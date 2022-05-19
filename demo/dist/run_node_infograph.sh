#! /bin/bash
#
# Tencent is pleased to support the open source community by making embedx
# available.
#
# Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
#
# Licensed under the BSD 3-Clause License and other third-party components,
# please refer to LICENSE for details.
#
# Author: Shuting Guo (shutingnjupt@gmail.com)
#

set -e
cd "$(dirname "$0")"
source runtime.sh

readonly DATASET="cora"
readonly DATASET_DIR="${DEMO_DIR}/data/${DATASET}"
readonly TRAIN_LABELS="${DATASET_DIR}/train_labels"
readonly TEST_LABELS="${DATASET_DIR}/test_labels"

readonly PS_NUM=2
readonly WK_NUM=8

# graph flags
readonly FLAGS_dist=1
readonly FLAGS_gs_addrs="127.0.0.1:8888;127.0.0.1:8889"
readonly FLAGS_gs_shard_num=2
readonly FLAGS_gs_thread_num=8
readonly FLAGS_node_graph="${DATASET_DIR}/context"
readonly FLAGS_node_feature="${DATASET_DIR}/node_feature"

# trainer & predictor flags
readonly FLAGS_cs_addr="127.0.0.1:61000"
readonly FLAGS_ps_addrs="127.0.0.1:60000;127.0.0.1:60001"
readonly FLAGS_model="node_infograph"
readonly FLAGS_model_config="config=0:54:64;depth=2;dim=128;alpha=0.1;sparse=1;use_neigh_feat=0;max_label=6;multi_label=0"
readonly FLAGS_instance_reader="node_infograph_inst_reader"
readonly FLAGS_optimizer="adam"
readonly FLAGS_optimizer_config="rho1=0.9;rho2=0.999;alpha=1e-4;beta=1e-8"
readonly FLAGS_epoch=200
readonly FLAGS_batch=512
readonly FLAGS_in="${FLAGS_node_graph}"
readonly FLAGS_inst_file="${TRAIN_LABELS}"
readonly FLAGS_out_model="model"
readonly FLAGS_out_predict="embedding"

################################################################
# Train
################################################################
FLAGS_sub_command="train"
FLAGS_instance_reader_config="num_neg=10;num_neighbors=50,10;use_neigh_feat=0;shuffle_type=0;instance_sample_prob=0.2;max_label=6;multi_label=0;train_data_type=1"
FLAGS_target_type=0
run_graph_server ${DATASET}
run_dist_server ${DATASET} ${PS_NUM}
run_dist_worker ${DATASET} ${WK_NUM}
wait_task_finish "ps" ${PS_NUM}
close_graph_server

################################################################
# Predict
################################################################
FLAGS_sub_command="predict"
FLAGS_instance_reader_config="num_neighbors=50,10;use_neigh_feat=0;is_train=0"
FLAGS_in_model="${FLAGS_out_model}"
FLAGS_target_type=2
run_graph_server ${DATASET}
run_dist_server ${DATASET} ${PS_NUM}
run_dist_worker ${DATASET} ${WK_NUM}
wait_task_finish "ps" ${PS_NUM}
close_graph_server

################################################################
# Evaluate
################################################################
evaluate_embedding "${TEST_LABELS}" "${TRAIN_LABELS}" "${FLAGS_out_predict}" "SGD"
