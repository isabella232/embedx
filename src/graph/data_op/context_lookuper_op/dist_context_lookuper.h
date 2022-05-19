// Tencent is pleased to support the open source community by making embedx
// available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
//
// Licensed under the BSD 3-Clause License and other third-party components,
// please refer to LICENSE for details.
//
// Author: Yuanhang Zou (yuanhang.nju@gmail.com)
//

#pragma once
#include <vector>

#include "src/common/data_types.h"
#include "src/graph/data_op/gs_op.h"

namespace embedx {
namespace graph_op {

class DistContextLookuper : public DistGSOp {
 public:
  ~DistContextLookuper() override = default;

 public:
  bool Run(const vec_int_t& nodes, std::vector<vec_pair_t>* contexts) const;
};

}  // namespace graph_op
}  // namespace embedx
