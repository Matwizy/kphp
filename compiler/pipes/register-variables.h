#pragma once

#include <map>

#include "compiler/data/var-data.h"
#include "compiler/function-pass.h"

/**
 * 1. Function parametres (with default values)
 * 2. Global variables
 * 3. Static local variables (with default values)
 * 4. Local variables
 * 5. Class static fields
 */
class RegisterVariablesPass final : public FunctionPassBase {
private:
  std::map<string, VarPtr> registred_vars;
  bool global_function_flag{false};
  int in_param_list{0};

  VarPtr create_global_var(const string &name);
  VarPtr create_local_var(const string &name, VarData::Type type, bool create_flag);
  VarPtr get_global_var(const string &name);
  VarPtr get_local_var(const string &name, VarData::Type type = VarData::var_local_t);
  void register_global_var(VertexAdaptor<op_var> var_vertex);
  bool is_const(VertexPtr v);
  bool is_global_var(VertexPtr v);
  void register_function_static_var(VertexAdaptor<op_var> var_vertex, VertexPtr default_value);
  void register_param_var(VertexAdaptor<op_func_param> param, VertexPtr default_value);
  void register_var(VertexAdaptor<op_var> var_vertex);
  void visit_global_vertex(VertexAdaptor<op_global> global);
  void visit_static_vertex(VertexAdaptor<op_static> stat);
  void visit_var(VertexAdaptor<op_var> var);

  void visit_func_param_list(VertexAdaptor<op_func_param_list> list);


public:

  string get_description() override {
    return "Register variables";
  }

  bool check_function(FunctionPtr function) const override {
    return !function->is_extern();
  }

  bool on_start(FunctionPtr function) override {
    FunctionPassBase::on_start(function);
    global_function_flag = function->type == FunctionData::func_global ||
                           function->type == FunctionData::func_switch;
    return true;
  }

  VertexPtr on_enter_vertex(VertexPtr root) override;
  VertexPtr on_exit_vertex(VertexPtr root) override;

  bool user_recursion(VertexPtr v) override;

};
