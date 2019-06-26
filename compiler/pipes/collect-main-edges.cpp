#include "compiler/pipes/collect-main-edges.h"

#include "compiler/compiler-core.h"
#include "compiler/data/define-data.h"
#include "compiler/data/var-data.h"
#include "compiler/function-pass.h"
#include "compiler/inferring/edge.h"
#include "compiler/inferring/ifi.h"
#include "compiler/inferring/lvalue.h"
#include "compiler/inferring/public.h"
#include "compiler/inferring/restriction-greater.h"
#include "compiler/inferring/restriction-isset.h"
#include "compiler/inferring/restriction-less.h"
#include "compiler/inferring/restriction-non-void.h"
#include "compiler/inferring/rvalue.h"
#include "compiler/inferring/type-node.h"
#include "compiler/scheduler/task.h"
#include "compiler/utils/string-utils.h"

tinf::Node *CollectMainEdgesPass::node_from_rvalue(const RValue &rvalue) {
  if (rvalue.node == nullptr) {
    kphp_assert (rvalue.type != nullptr);
    return new tinf::TypeNode(rvalue.type, stage::get_location());
  }

  return rvalue.node;
}

void CollectMainEdgesPass::require_node(const RValue &rvalue) {
  if (rvalue.node != nullptr) {
    tinf::get_inferer()->add_node(rvalue.node);
  }
}

void CollectMainEdgesPass::create_set(const LValue &lvalue, const RValue &rvalue) {
  tinf::Edge *edge = new tinf::Edge();
  edge->from = lvalue.value;
  edge->from_at = lvalue.key;
  edge->to = node_from_rvalue(rvalue);
  tinf::get_inferer()->add_edge(edge);
  tinf::get_inferer()->add_node(edge->from);
}

template<class RestrictionT>
void CollectMainEdgesPass::create_restriction(const RValue &lhs, const RValue &rhs) {
  tinf::Node *a = node_from_rvalue(lhs);
  tinf::Node *b = node_from_rvalue(rhs);
  tinf::get_inferer()->add_node(a);
  tinf::get_inferer()->add_node(b);
  tinf::get_inferer()->add_restriction(new RestrictionT(a, b));
}

void CollectMainEdgesPass::create_less(const RValue &lhs, const RValue &rhs) {
  create_restriction<RestrictionLess>(lhs, rhs);
}

void CollectMainEdgesPass::create_greater(const RValue &lhs, const RValue &rhs) {
  create_restriction<RestrictionGreater>(lhs, rhs);
}

void CollectMainEdgesPass::create_non_void(const RValue &lhs) {
  tinf::Node *a = node_from_rvalue(lhs);
  tinf::get_inferer()->add_node(a);
  tinf::get_inferer()->add_restriction(new RestrictionNonVoid(a));
}


void CollectMainEdgesPass::create_isset_check(const RValue &rvalue) {
  tinf::Node *a = node_from_rvalue(rvalue);
  tinf::get_inferer()->add_node(a);
  tinf::get_inferer()->add_restriction(new RestrictionIsset(a));
}

RValue CollectMainEdgesPass::as_set_value(VertexPtr v) {
  if (v->type() == op_set) {
    return as_rvalue(v.as<op_set>()->rhs());
  }

  if (v->type() == op_prefix_inc ||
      v->type() == op_prefix_dec ||
      v->type() == op_postfix_dec ||
      v->type() == op_postfix_inc) {
    auto unary = v.as<meta_op_unary>();
    auto one = VertexAdaptor<op_int_const>::create();
    auto res = VertexAdaptor<op_add>::create(unary->expr(), one);
    set_location(one, stage::get_location());
    set_location(res, stage::get_location());
    return as_rvalue(res);
  }

  if (OpInfo::arity(v->type()) == binary_opp) {
    auto binary = v.as<meta_op_binary>();
    VertexPtr res = create_vertex(OpInfo::base_op(v->type()), binary->lhs(), binary->rhs());
    set_location(res, stage::get_location());
    return as_rvalue(res);
  }

  kphp_fail();
  return RValue();
}

template<class A, class B>
void CollectMainEdgesPass::create_set(const A &a, const B &b) {
  create_set(as_lvalue(a), as_rvalue(b));
}

template<class A, class B>
void CollectMainEdgesPass::create_less(const A &a, const B &b) {
  create_less(as_rvalue(a), as_rvalue(b));
}

template<class A, class B>
void CollectMainEdgesPass::create_greater(const A &a, const B &b) {
  create_greater(as_rvalue(b), as_rvalue(a));
}

template<class A>
void CollectMainEdgesPass::require_node(const A &a) {
  require_node(as_rvalue(a));
}

template<class A>
void CollectMainEdgesPass::create_non_void(const A &a) {
  create_non_void(as_rvalue(a));
}


void CollectMainEdgesPass::add_type_rule(VertexPtr v) {
  if (v->type() == op_function) {
    return;
  }

  switch (v->type_rule->type()) {
    case op_common_type_rule:
      create_set(v, v->type_rule);
      break;
    case op_gt_type_rule:
      create_greater(v, v->type_rule);
      break;
    case op_lt_type_rule:
      create_less(v, v->type_rule);
      break;
    case op_eq_type_rule:
      create_less(v->type_rule, v);
      create_less(v, v->type_rule);
      break;
    case op_set_check_type_rule:
      create_set(v, v->type_rule);
      create_less(v, v->type_rule);
      break;
    default:
      assert (0 && "unreachable");
  }
}

void CollectMainEdgesPass::add_type_help(VertexPtr v) {
  if (v->type() != op_var) {
    return;
  }
  create_set(v, v->type_help);
}

void CollectMainEdgesPass::on_func_param_callback(VertexAdaptor<op_func_call> call, int id) {
  const FunctionPtr call_function = call->get_func_id();
  const VertexPtr ith_argument_of_call = call->args()[id];
  auto callback_param = call_function->get_params()[id].as<op_func_param_callback>();

  FunctionPtr callback_function;
  if (ith_argument_of_call->type() == op_func_ptr) {
    callback_function = ith_argument_of_call->get_func_id();
  }

  kphp_assert(callback_function);

  // // restriction on return type
  bool is_any = false;
  if (auto rule = callback_param->type_rule.try_as<op_common_type_rule>()) {
    if (auto son = rule->rule().try_as<op_type_expr_type>()) {
      is_any = son->type_help == tp_Any;

      if (!is_any && callback_function && callback_function->is_extern()) {
        if (auto rule_of_callback = callback_function->root->type_rule.try_as<op_common_type_rule>()) {
          if (auto son_of_callback_rule = rule_of_callback->rule().try_as<op_type_expr_type>()) {
            is_any = son_of_callback_rule->type_help == son->type_help;
          }
        }
      }
    }
  }

  if (!is_any) {
    auto fake_func_call = VertexAdaptor<op_func_call>::create(call->get_next());
    fake_func_call->type_rule = callback_param->type_rule;
    fake_func_call->set_func_id(call_function);
    create_less(as_rvalue(callback_function, -1), fake_func_call);
  } else {
    create_non_void(as_rvalue(callback_function, -1));
  }

  VertexRange callback_args = get_function_params(callback_param);
  for (int i = 0; i < callback_args.size(); ++i) {
    auto callback_ith_arg = callback_args[i].as<op_func_param>();

    if (VertexPtr type_rule = callback_ith_arg->type_rule) {
      auto fake_func_call = VertexAdaptor<op_func_call>::create(call->get_next());
      fake_func_call->type_rule = type_rule;
      fake_func_call->set_func_id(call_function);

      int id_of_callbak_argument = callback_function->is_lambda() ? i + 1 : i;
      create_set(as_lvalue(callback_function, id_of_callbak_argument), fake_func_call);
    }
  }
}

void CollectMainEdgesPass::on_func_call(VertexAdaptor<op_func_call> call) {
  FunctionPtr function = call->get_func_id();
  VertexRange function_params = function->get_params();

  //hardcoded hack
  if (function->name == "array_unshift" || function->name == "array_push") {
    VertexRange args = call->args();
    LValue val = as_lvalue(args[0]);

    auto key = new MultiKey(*val.key);
    key->push_back(Key::any_key());
    val.key = key;

    for (auto i : VertexRange(args.begin() + 1, args.end())) {
      create_set(val, i);
    }
  }

  if (function->name == "wait_queue_push") {
    VertexRange args = call->args();
    LValue val = as_lvalue(args[0]);

    auto fake_func_call = VertexAdaptor<op_func_call>::create(call->get_next());
    auto ref = VertexAdaptor<op_type_expr_arg_ref>::create();
    ref->int_val = 2;
    VertexPtr rule = VertexAdaptor<op_index>::create(ref);
    rule = VertexAdaptor<op_type_expr_type>::create(rule);
    rule->type_help = tp_future_queue;
    rule = VertexAdaptor<op_common_type_rule>::create(rule);
    fake_func_call->type_rule = rule;
    fake_func_call->set_func_id(call->get_func_id());

    create_set(val, fake_func_call);
  }


  if (function->has_variadic_param) {
    auto id_of_last_param = function_params.size() - 1;
    RValue array_of_any{TypeData::get_type(tp_array, tp_Any)};
    create_less(as_rvalue(function, id_of_last_param), array_of_any);
  }

  for (int i = 0; i < call->args().size(); ++i) {
    VertexPtr arg = call->args()[i];
    auto param = function_params[i].as<meta_op_func_param>();

    if (param->type() == op_func_param_callback) {
      on_func_param_callback(call, i);
    } else {
      if (!function->is_extern()) {
        create_set(as_lvalue(function, i), arg);
      }

      if (param->var()->ref_flag) {
        create_set(arg, as_rvalue(function, i));
      }
    }
  }
}

void CollectMainEdgesPass::on_constructor_call(VertexAdaptor<op_constructor_call> call) {
  FunctionPtr function = call->get_func_id();
  VertexRange function_params = function->get_params();

  int ii = 0;
  for (auto arg : call->args()) {

    create_set(as_lvalue(function, ii), arg);

    auto param = function_params[ii].as<meta_op_func_param>();
    if (param->var()->ref_flag) {
      create_set(arg, as_rvalue(function, ii));
    }

    ii++;
  }
}

void CollectMainEdgesPass::on_return(VertexAdaptor<op_return> v) {
  have_returns = true;
  if (v->has_expr()) {
    create_set(as_lvalue(stage::get_function(), -1), v->expr());
  } else {
    create_set(as_lvalue(stage::get_function(), -1), tp_void);
  }
}

void CollectMainEdgesPass::on_foreach(VertexAdaptor<op_foreach> foreach_op) {
  auto params = foreach_op->params();
  VertexPtr xs, x, key, temp_var;
  xs = params->xs();
  x = params->x();
  temp_var = params->temp_var();
  if (params->has_key()) {
    key = params->key();
  }
  if (x->ref_flag) {
    LValue xs_tinf = as_lvalue(xs);
    create_set(xs_tinf, tp_array);
    create_set(params, x->get_var_id());
  } else {
    create_set(temp_var->get_var_id(), xs);
  }
  create_set(x->get_var_id(), params);
  if (key) {
    create_set(key->get_var_id(), tp_var);
  }
}

void CollectMainEdgesPass::on_list(VertexAdaptor<op_list> list) {
  int i = 0;
  for (auto cur : list->list()) {
    if (cur->type() != op_lvalue_null) {
      // делаем $cur = $list_array[$i]; хотелось бы array[i] выразить через rvalue multikey int_key, но
      // при составлении edges (from_node[from_at] = to_node) этот key теряется, поэтому через op_index
      auto ith_index = VertexAdaptor<op_int_const>::create();
      ith_index->set_string(int_to_str(i));
      auto new_v = VertexAdaptor<op_index>::create(list->array(), ith_index);
      set_location(new_v, stage::get_location());
      create_set(cur, new_v);
    }
    i++;
  }
}

void CollectMainEdgesPass::on_throw(VertexAdaptor<op_throw> throw_op) {
  create_less(G->get_class("Exception"), throw_op->exception());
  create_less(throw_op->exception(), G->get_class("Exception"));
}

void CollectMainEdgesPass::on_try(VertexAdaptor<op_try> try_op) {
  create_set(try_op->exception(), G->get_class("Exception"));
}

void CollectMainEdgesPass::on_set_op(VertexPtr v) {
  VertexPtr lval;
  if (OpInfo::arity(v->type()) == binary_opp) {
    lval = v.as<meta_op_binary>()->lhs();
  } else if (OpInfo::arity(v->type()) == unary_opp) {
    lval = v.as<meta_op_unary>()->expr();
  } else {
    kphp_fail();
  }
  create_set(lval, as_set_value(v));
}
void CollectMainEdgesPass::ifi_fix(VertexPtr v) {
  is_func_id_t ifi_tp = get_ifi_id(v);
  if (ifi_tp == ifi_error) {
    return;
  }
  for (auto cur : *v) {
    if (cur->type() == op_var && cur->get_var_id()->is_constant()) {
      continue;
    }

    if (cur->type() == op_var && (ifi_tp == ifi_unset || ifi_tp == ifi_isset || ifi_tp == ifi_is_null)) {
      create_set(cur, tp_var);
    }

    if ((cur->type() == op_var && ifi_tp != ifi_unset) || (ifi_tp > ifi_isset && cur->type() == op_index)) {
      tinf::Node *node = tinf::get_tinf_node(cur);
      if (node->isset_flags == 0) {
        create_isset_check(as_rvalue(node));
      }
      node->isset_flags |= ifi_tp;
    }
  }
}

void CollectMainEdgesPass::on_class(ClassPtr klass) {
  klass->members.for_each([&](ClassMemberInstanceField field) {
    on_var(field.var);
  });
}

void CollectMainEdgesPass::on_function(FunctionPtr function) {
  VertexRange params = function->get_params();
  int params_n = (int)params.size();

  for (int i = -1; i < params_n; i++) {
    require_node(as_rvalue(function, i));
  }

  if (function->is_extern()) {
    PrimitiveType ret_type = function->root->type_help;
    if (ret_type == tp_Unknown) {
      ret_type = tp_var;
    }
    create_set(as_lvalue(function, -1), ret_type);

    for (int i = 0; i < params_n; i++) {
      PrimitiveType ptype = params[i]->type_help;
      if (ptype == tp_Unknown) {
        ptype = tp_Any;
      }
      //FIXME: type is const...
      create_set(as_lvalue(function, i), TypeData::get_type(ptype, tp_Any));
    }
  } else {
    for (int i = 0; i < params_n; i++) {
      //FIXME?.. just use pointer to node?..
      create_set(as_lvalue(function, i), function->param_ids[i]);
      create_set(function->param_ids[i], as_rvalue(function, i));
    }

    // @kphp-infer hint/check для @param/@return — это less/set на соответствующие tinf_nodes функции
    for (const FunctionData::InferHint &hint : function->infer_hints) {
      switch (hint.infer_type) {
        case FunctionData::InferHint::infer_mask::check:
          create_less(as_rvalue(function, hint.param_i), hint.type_rule);
          break;
        case FunctionData::InferHint::infer_mask::hint:
          create_set(as_lvalue(function, hint.param_i), hint.type_rule);
          break;
        case FunctionData::InferHint::infer_mask::cast:
          // ничего не делаем, т.к. там просто поставился type_help в parse_and_apply_function_kphp_phpdoc()
          break;
      }
    }

    Assumption return_assumption = function->assumption_for_return;
    RValue rvalue_of_return = as_rvalue(function, -1);
    switch (return_assumption.assum_type) {
      case AssumType::assum_instance:
        create_less(rvalue_of_return, return_assumption.klass->type_data);
        break;

      case AssumType::assum_instance_array:
        create_less(rvalue_of_return, TypeData::create_array_type_data(return_assumption.klass->type_data, true));
        break;

      default:
        break;
    }
  }
}

bool CollectMainEdgesPass::on_start(FunctionPtr function) {
  if (!FunctionPassBase::on_start(function)) {
    return false;
  }
  if (function->type == FunctionData::func_class_holder) {
    on_class(function->class_id);
  }
  on_function(function);
  return !function->is_extern();
}

VertexPtr CollectMainEdgesPass::on_enter_vertex(VertexPtr v, FunctionPassBase::LocalT *) {
  if (v->type() == op_try) {
    auto try_v = v.as<op_try>();
    if (try_v->exception()->type() == op_empty && try_v->catch_cmd()->type() == op_empty) {
      return try_v->try_cmd();
    }
  }

  if (v->type_rule) {
    add_type_rule(v);
  }
  //FIXME: type_rule should be used indead of type_help
  if (v->type_help != tp_Unknown) {
    add_type_help(v);
  }

  switch (v->type()) {
    //FIXME: has_variadic_param
    case op_func_call:
      on_func_call(v.as<op_func_call>());
      break;
    case op_constructor_call:
      on_constructor_call(v.as<op_constructor_call>());
      break;
    case op_return:
      on_return(v.as<op_return>());
      break;
    case op_foreach:
      on_foreach(v.as<op_foreach>());
      break;
    case op_list:
      on_list(v.as<op_list>());
      break;
    case op_throw:
      on_throw(v.as<op_throw>());
      break;
    case op_try:
      on_try(v.as<op_try>());
      break;
    default:
      break;
  }
  if (OpInfo::rl(v->type()) == rl_set ||
      v->type() == op_prefix_inc ||
      v->type() == op_postfix_inc ||
      v->type() == op_prefix_dec ||
      v->type() == op_postfix_dec) {
    on_set_op(v);
  }

  ifi_fix(v);

  return v;
}

void CollectMainEdgesPass::on_var(VarPtr var) {
  if (var->tinf_flag) {
    return;
  }
  if (!__sync_bool_compare_and_swap(&var->tinf_flag, false, true)) {
    return;
  }
  require_node(var);
  if (var->init_val) {
    create_set(var, var->init_val);
  }

  // для всех переменных-инстансов (локальные, параметры и т.п.) делаем restriction'ы, что классы те же что в phpdoc
  ClassPtr cl;
  AssumType assum = assum_unknown;
  if (var->is_class_instance_var()) {
    assum = assumption_get_for_var(var->class_id, var->name, cl);
  } else {
    if (!current_function->assumptions_for_vars.empty()) {
      assum = assumption_get_for_var(current_function, var->name, cl);
    }
  }
  if (assum == assum_instance) {                  // var == cl
    create_less(var, cl->type_data);
    // You could specify php-doc that some var is `Interface class` but always assign to that var only one class.
    // In this situation we will infer that type of this var is concrete class not Interface
    // create_less(cl->type_data, var);
  } else if (assum == assum_instance_array) {     // cl[] <= var <= OrFalse<cl[]>
    create_less(var, TypeData::create_array_type_data(cl->type_data, true));
    create_less(TypeData::create_array_type_data(cl->type_data), var);
  }
}

std::nullptr_t CollectMainEdgesPass::on_finish() {
  if (!have_returns) {
    // hack to work well with functions which always throws
    create_set(as_lvalue(current_function, -1), tp_void);
  }
  call_on_var(current_function->local_var_ids);
  call_on_var(current_function->global_var_ids);
  call_on_var(current_function->static_var_ids);
  call_on_var(current_function->implicit_const_var_ids);
  call_on_var(current_function->explicit_const_var_ids);
  call_on_var(current_function->explicit_header_const_var_ids);
  call_on_var(current_function->param_ids);
  return {};
}

template<class CollectionT>
void CollectMainEdgesPass::call_on_var(const CollectionT &collection) {
  for (const auto &el: collection) {
    on_var(el);
  }
}

// хотелось сделать, чтобы при записи $a[6][$idx] = ... делался честный multikey (int 6, any), а не AnyKey(2)
// но у нас в реальном коде очень много числовых индексов на массивах, которые тогда хранятся отдельно,
// и потом вывод типов отжирает слишком много памяти, т.к. хранит кучу индексов, а не просто any key
// так что затея провалилась, и, как и раньше, при $a[...] делается AnyKey; поэтому же tuple'ы только read-only
// но это только запись приводит в any key! с чтением всё в порядке, см. recalc_index() в выводе типов
/*
MultiKey *build_real_multikey (VertexPtr v) {
  MultiKey *key = new MultiKey();

  if (v->type() == op_foreach_param) {
    key->push_front(Key::any_key());
    v = v.as <op_foreach_param>()->xs();
  }
  while (v->type() == op_index) {
    if (v.as <op_index>()->has_key() && v.as <op_index>()->key()->type() == op_int_const) {
      key->push_front(Key::int_key(std::atoi(v.as <op_index>()->key()->get_string().c_str())));
    } else {
      key->push_front(Key::any_key());
    }
    v = v.as <op_index>()->array();
  }

  return key;
}
*/
