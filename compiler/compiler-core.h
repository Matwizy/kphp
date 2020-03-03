#pragma once


/*** Core ***/
//Consists mostly of functions that require synchronization

#include "common/algorithms/hashes.h"

#include "compiler/data/data_ptr.h"
#include "compiler/enviroment.h"
#include "compiler/index.h"
#include "compiler/stats.h"
#include "compiler/threading/data-stream.h"
#include "compiler/threading/hash-table.h"
#include "compiler/tl-classes.h"

class CompilerCore {
private:
  Index cpp_index;
  TSHashTable<SrcFilePtr> file_ht;
  TSHashTable<FunctionPtr> functions_ht;
  TSHashTable<DefinePtr> defines_ht;
  TSHashTable<VarPtr> global_vars_ht;
  TSHashTable<LibPtr> libs_ht;
  vector<SrcFilePtr> main_files;
  KphpEnviroment *env_;
  TSHashTable<ClassPtr> classes_ht;
  ClassPtr memcache_class;
  TlClasses tl_classes;

  inline bool try_require_file(SrcFilePtr file);

public:
  string cpp_dir;
  bool server_tl_serialization_used{false};

  CompilerCore();
  void start();
  void finish();
  void register_env(KphpEnviroment *env);
  const KphpEnviroment &env() const;
  const string &get_global_namespace() const;
  string unify_file_name(const string &file_name);
  std::string search_file_in_include_dirs(const std::string &file_name, size_t *dir_index = nullptr) const;
  SrcFilePtr register_file(const string &file_name, LibPtr owner_lib);

  void register_main_file(const string &file_name, DataStream<SrcFilePtr> &os);
  SrcFilePtr require_file(const string &file_name, LibPtr owner_lib, DataStream<SrcFilePtr> &os, bool error_if_not_exists = true);

  void require_function(const string &name, DataStream<FunctionPtr> &os);
  void require_function(FunctionPtr function, DataStream<FunctionPtr> &os);

  template <class CallbackT>
  void operate_on_function_locking(const string &name, CallbackT callback) {
    static_assert(std::is_constructible<std::function<void(FunctionPtr&)>, CallbackT>::value, "invalid callback signature");

    TSHashTable<FunctionPtr>::HTNode *node = functions_ht.at(vk::std_hash(name));
    AutoLocker<Lockable *> locker(node);
    callback(node->data);
  }

  void register_function(FunctionPtr function);
  void register_and_require_function(FunctionPtr function, DataStream<FunctionPtr> &os, bool force_require = false);
  bool register_class(ClassPtr cur_class);
  LibPtr register_lib(LibPtr lib);

  FunctionPtr get_function(const string &name);
  ClassPtr get_class(vk::string_view name);
  ClassPtr get_memcache_class();
  void set_memcache_class(ClassPtr klass);

  bool register_define(DefinePtr def_id);
  DefinePtr get_define(const string &name);

  VarPtr create_var(const string &name, VarData::Type type);
  VarPtr get_global_var(const string &name, VarData::Type type, VertexPtr init_val, bool *is_new_inserted = nullptr);
  VarPtr create_local_var(FunctionPtr function, const string &name, VarData::Type type);

  const vector<SrcFilePtr> &get_main_files();
  vector<VarPtr> get_global_vars();
  vector<ClassPtr> get_classes();
  vector<DefinePtr> get_defines();
  vector<LibPtr> get_libs();

  void load_index();
  void save_index();
  const Index &get_index();
  File *get_file_info(const string &file_name);
  void del_extra_files();
  void init_dest_dir();
  std::string get_subdir_name() const;

  void try_load_tl_classes();
  const TlClasses &get_tl_classes() const { return tl_classes; }

  Stats stats;
};

extern CompilerCore *G;

/*** Misc functions ***/
bool try_optimize_var(VarPtr var);
string conv_to_func_ptr_name(VertexPtr call);
VertexPtr conv_to_func_ptr(VertexPtr call);

