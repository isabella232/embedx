// Tencent is pleased to support the open source community by making embedx
// available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
//
// Licensed under the BSD 3-Clause License and other third-party components,
// please refer to LICENSE for details.
//
// Author: Litao Hong (Lthong.brian@gmail.com)
//         Yuanhang Zou (yuanhang.nju@gmail.com)
//

#include <deepx_core/common/str_util.h>
#include <deepx_core/dx_log.h>

#include <vector>

#include "src/io/indexing.h"
#include "src/io/value.h"
#include "src/model/data_flow/neighbor_aggregation_flow.h"
#include "src/model/embed_instance_reader.h"
#include "src/model/instance_node_name.h"
#include "src/model/instance_reader_util.h"

namespace embedx {

class NodeInfoGraphInstReader : public EmbedInstanceReader {
 private:
  bool is_train_ = true;
  int num_neg_ = 5;
  bool use_neigh_feat_ = false;
  std::vector<int> num_neighbors_;
  int shuffle_type_ = 0;
  int train_data_type_ = 0;
  int num_label_ = 1;
  int max_label_ = 1;
  bool multi_label_ = false;
  double instance_sample_prob_ = 0.0;

 private:
  std::unique_ptr<NeighborAggregationFlow> flow_;

  vec_int_t src_nodes_;
  std::vector<vec_int_t> seqs_;
  vec_int_t labeled_nodes_;
  std::vector<vecl_t> vec_labels_list_;

  vec_int_t merged_nodes_;
  vec_set_t level_nodes_;
  vec_int_t shuffled_nodes_;
  vec_map_neigh_t level_neighbors_;
  std::vector<Indexing> indexings_;

 public:
  DEFINE_INSTANCE_READER_LIKE(NodeInfoGraphInstReader);

 public:
  bool InitGraphClient(const GraphClient* graph_client) override {
    if (!EmbedInstanceReader::InitGraphClient(graph_client)) {
      return false;
    }

    flow_ = NewNeighborAggregationFlow(graph_client);
    return true;
  }

 protected:
  bool InitConfigKV(const std::string& k, const std::string& v) override {
    if (InstanceReaderImpl::InitConfigKV(k, v)) {
    } else if (k == "num_neg") {
      num_neg_ = std::stoi(v);
      DXCHECK(num_neg_ > 0);
    } else if (k == "num_neighbors") {
      DXCHECK(deepx_core::Split<int>(v, ",", &num_neighbors_));
    } else if (k == "use_neigh_feat") {
      auto val = std::stoi(v);
      DXCHECK(val == 1 || val == 0);
      use_neigh_feat_ = val;
    } else if (k == "is_train") {
      auto val = std::stoi(v);
      DXCHECK(val == 1 || val == 0);
      is_train_ = val;
    } else if (k == "shuffle_type") {
      auto val = std::stoi(v);
      DXCHECK(val == 1 || val == 0);
      shuffle_type_ = val;
    } else if (k == "train_data_type") {
      auto val = std::stoi(v);
      DXCHECK(val == 1 || val == 0);
      train_data_type_ = val;
    } else if (k == "instance_sample_prob") {
      instance_sample_prob_ = std::stod(v);
      DXCHECK(instance_sample_prob_ >= 0.0 && instance_sample_prob_ <= 1.0);
    } else if (k == "num_label") {
      num_label_ = std::stoi(v);
      DXCHECK(num_label_ >= 1);
    } else if (k == "multi_label") {
      auto val = std::stoi(v);
      DXCHECK(val == 1 || val == 0);
      multi_label_ = val;
    } else if (k == "max_label") {
      max_label_ = std::stoi(v);
      DXCHECK(max_label_ >= 1);
    } else {
      DXERROR("Unexpected config: %s = %s.", k.c_str(), v.c_str());
      return false;
    }

    DXINFO("Instance reader argument: %s = %s.", k.c_str(), v.c_str());
    return true;
  }

  bool GetBatch(Instance* inst) override {
    return is_train_ ? GetTrainBatch(inst) : GetPredictBatch(inst);
  }

  /************************************************************************/
  /* Read batch data from file for training */
  /************************************************************************/
  bool GetTrainBatch(Instance* inst) {
    // sequence input
    if (train_data_type_ == 0) {  // get batch sequence
      std::vector<SeqValue> values;
      if (!NextInstanceBatch<SeqValue>(inst, batch_, &values)) {
        return false;
      }
      seqs_ = Collect<SeqValue, vec_int_t>(values, &SeqValue::nodes);
      flow_->MergeTo(seqs_, &src_nodes_);
    } else {  // src adjlist input
      std::vector<NodeValue> values;
      if (!NextInstanceBatch<NodeValue>(inst, batch_, &values)) {
        return false;
      }
      src_nodes_ = Collect<NodeValue, int_t>(values, &NodeValue::node);
    }

    int instance_sample_count =
        (int)(instance_sample_prob_ * src_nodes_.size());
    DXCHECK(instance_sample_count > 0);
    DXCHECK(deep_client_->SampleInstance(instance_sample_count, &labeled_nodes_,
                                         &vec_labels_list_));

    // merge nodes
    merged_nodes_.clear();
    flow_->MergeTo(src_nodes_, &merged_nodes_);
    flow_->MergeTo(labeled_nodes_, &merged_nodes_);

    // sample subgraph
    flow_->SampleSubGraph(merged_nodes_, num_neighbors_, &level_nodes_,
                          &level_neighbors_);

    // Fill instance
    // 1. Fill node feature
    flow_->FillLevelNodeFeature(inst, instance_name::X_NODE_FEATURE_NAME,
                                level_nodes_);

    // build shuffled node feature
    if (shuffle_type_ == 0) {
      flow_->ShuffleNodesInBatch(level_nodes_, &shuffled_nodes_);
    } else {
      flow_->ShuffleNodesInGlobal(level_nodes_, &shuffled_nodes_);
    }

    // 2. Fill shuffled node feature
    flow_->FillNodeFeature(inst, instance_name::X_NODE_SHUFFLED_FEATURE_NAME,
                           shuffled_nodes_, false);

    // 3. Fill self and neighbor block
    inst_util::CreateIndexings(level_nodes_, &indexings_);
    flow_->FillSelfAndNeighGraphBlock(inst, instance_name::X_SELF_BLOCK_NAME,
                                      instance_name::X_NEIGH_BLOCK_NAME,
                                      level_nodes_, level_neighbors_,
                                      indexings_, false);

    // 4. Fill src_nodes and label_nodes index
    flow_->FillNodeOrIndex(inst, instance_name::X_SRC_ID_NAME, src_nodes_,
                           &indexings_[0]);
    flow_->FillNodeOrIndex(inst, instance_name::X_NODE_ID_NAME, labeled_nodes_,
                           &indexings_[0]);

    // 6. Fill label
    flow_->FillLabelAndCheck(inst, deepx_core::Y_NAME, vec_labels_list_,
                             num_label_, max_label_);

    inst->set_batch(src_nodes_.size());
    return true;
  }

  /************************************************************************/
  /* Read batch data from file for prediction */
  /************************************************************************/
  bool GetPredictBatch(Instance* inst) {
    std::vector<NodeValue> values;
    if (!NextInstanceBatch<NodeValue>(inst, batch_, &values)) {
      return false;
    }
    src_nodes_ = Collect<NodeValue, int_t>(values, &NodeValue::node);

    // Sample subgraph
    flow_->SampleSubGraph(src_nodes_, num_neighbors_, &level_nodes_,
                          &level_neighbors_);

    // Fill Instance
    // 1. Fill node and neighbor feature
    flow_->FillLevelNodeFeature(inst, instance_name::X_NODE_FEATURE_NAME,
                                level_nodes_);
    flow_->FillLevelNeighFeature(inst, instance_name::X_NEIGH_FEATURE_NAME,
                                 level_nodes_);

    // 2. Fill self and neighbor block
    inst_util::CreateIndexings(level_nodes_, &indexings_);
    flow_->FillSelfAndNeighGraphBlock(inst, instance_name::X_SELF_BLOCK_NAME,
                                      instance_name::X_NEIGH_BLOCK_NAME,
                                      level_nodes_, level_neighbors_,
                                      indexings_, false);

    // 3. Fill index
    flow_->FillNodeOrIndex(inst, instance_name::X_SRC_ID_NAME, src_nodes_,
                           &indexings_[0]);

    auto* predict_node_ptr =
        &inst->get_or_insert<vec_int_t>(instance_name::X_PREDICT_NODE_NAME);
    *predict_node_ptr = src_nodes_;
    inst->set_batch((int)src_nodes_.size());
    return true;
  }
};

INSTANCE_READER_REGISTER(NodeInfoGraphInstReader, "NodeInfoGraphInstReader");
INSTANCE_READER_REGISTER(NodeInfoGraphInstReader, "node_infograph_inst_reader");
}  // namespace embedx
