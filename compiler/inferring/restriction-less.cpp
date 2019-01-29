#include "compiler/inferring/restriction-less.h"

#include <iomanip>
#include <regex>

#include "common/termformat/termformat.h"

#include "compiler/data/var-data.h"
#include "compiler/inferring/expr-node.h"
#include "compiler/inferring/public.h"
#include "compiler/inferring/type-node.h"
#include "compiler/inferring/var-node.h"
#include "compiler/vertex.h"

/* эвристика упорядочивания ребер для поиска наиболее подходящего, для вывода ошибки на экран, полученная эмпирическим путем */
struct RestrictionLess::ComparatorByEdgePriorityRelativeToExpectedType {
private:
  enum {
    e_default_priority = 4
  };

  const TypeData *expected;

  static bool is_same_vars(const tinf::Node *node, VertexPtr vertex);

  int get_priority(const tinf::Edge *edge) const;

public:
  explicit ComparatorByEdgePriorityRelativeToExpectedType(const TypeData *expected) :
    expected(expected) {}

  bool is_priority_less_than_default(tinf::Node *cur_node) const;

  bool operator()(const tinf::Edge *lhs, const tinf::Edge *rhs) const {
    return get_priority(lhs) < get_priority(rhs);
  }
};

bool RestrictionLess::ComparatorByEdgePriorityRelativeToExpectedType::is_same_vars(const tinf::Node *node, VertexPtr vertex) {
  if (const tinf::VarNode *var_node = dynamic_cast<const tinf::VarNode *>(node)) {
    if (vertex->type() == op_var) {
      if (!vertex->get_var_id() && !var_node->var_) {
        return true;
      }

      return vertex->get_var_id() && var_node->var_ &&
             vertex->get_var_id()->name == var_node->var_->name;
    }
  }

  return false;
}

bool RestrictionLess::is_parent_node(tinf::Node const *node) {
  return std::find(node_path_.begin(), node_path_.end(), node) != node_path_.end();
}

bool RestrictionLess::find_call_trace_with_error_impl(tinf::Node *cur_node, const TypeData *expected) {
  static int limit_calls = 1000000;
  limit_calls--;
  if (limit_calls <= 0) {
    return true;
  }

  ComparatorByEdgePriorityRelativeToExpectedType comparator(expected);

  auto &node_next = cur_node->get_next();

  if (node_next.empty()) {
    return comparator.is_priority_less_than_default(cur_node);
  }

  std::vector<tinf::Edge *> ordered_edges{node_next};
  std::sort(ordered_edges.begin(), ordered_edges.end(), comparator);

  for (auto e : ordered_edges) {
    tinf::Node *from = e->from;
    tinf::Node *to = e->to;

    assert(from == cur_node);

    if (to == cur_node) {
      string warn_message = "loop edge: from: " + from->get_description() + "; to: " + to->get_description() + "\n";
      kphp_warning(warn_message.c_str());
      continue;
    }

    if (is_parent_node(to)) {
      continue;
    }

    if (node_path_.size() == max_cnt_nodes_in_path) {
      return false;
    }

    node_path_.push_back(to);

    const TypeData *expected_type_in_level_of_multi_key = expected;
    if (e->from_at) {
      expected_type_in_level_of_multi_key = expected->const_read_at(*e->from_at);
    }

    if (find_call_trace_with_error_impl(to, expected_type_in_level_of_multi_key)) {
      node_path_.pop_back();
      stacktrace.push_back(to);

      return true;
    }

    node_path_.pop_back();
  }

  return false;
}

bool RestrictionLess::is_less(const TypeData *given, const TypeData *expected, const MultiKey *from_at) {
  std::unique_ptr<TypeData> type_of_to_node(expected->clone());

  if (from_at) {
    type_of_to_node->set_lca_at(*from_at, given);
  } else {
    type_of_to_node->set_lca(given);
  }

  return type_out(type_of_to_node.get()) != type_out(expected);
}

void RestrictionLess::find_call_trace_with_error(tinf::Node *cur_node) {
  assert(cur_node != nullptr);

  stacktrace.clear();
  node_path_.clear();

  stacktrace.reserve(max_cnt_nodes_in_path);
  node_path_.reserve(max_cnt_nodes_in_path);

  find_call_trace_with_error_impl(cur_node, expected_->get_type());

  stacktrace.push_back(cur_node);
  std::reverse(stacktrace.begin(), stacktrace.end());
}

int RestrictionLess::ComparatorByEdgePriorityRelativeToExpectedType::get_priority(const tinf::Edge *edge) const {
  const tinf::Node *from_node = edge->from;
  const tinf::Node *to_node = edge->to;
  const TypeData *to_type = to_node->get_type();
  bool different_types = is_less(to_type, expected, edge->from_at);

  if (const tinf::ExprNode *expr_node = dynamic_cast<const tinf::ExprNode *>(to_node)) {
    VertexPtr expr_vertex = expr_node->get_expr();

    if (OpInfo::arity(expr_vertex->type()) == binary_opp) {
      auto binary_vertex = expr_vertex.as<meta_op_binary>();
      VertexPtr lhs = binary_vertex->lhs();
      VertexPtr rhs = binary_vertex->rhs();

      if (is_same_vars(from_node, lhs)) {
        to_type = tinf::get_type(rhs);
      } else if (is_same_vars(from_node, rhs)) {
        to_type = tinf::get_type(lhs);
      }
    } else {
      to_type = tinf::get_type(expr_vertex);
    }
  } else {
    if (const tinf::VarNode *var_node = dynamic_cast<const tinf::VarNode *>(to_node)) {
      if (var_node->is_argument_of_function()) {
        return different_types ? 1 : (e_default_priority + 1);
      }
    } else if (dynamic_cast<const tinf::TypeNode *>(to_node)) {
      return different_types ? 2 : (e_default_priority + 2);
    }
  }

  if (to_type->ptype() == tp_array && to_type->lookup_at(Key::any_key()) == nullptr) {
    return different_types ? 3 : (e_default_priority + 3);
  }

  if (is_less(to_type, expected, edge->from_at)) {
    return 0;
  }

  return e_default_priority;
}

bool RestrictionLess::ComparatorByEdgePriorityRelativeToExpectedType::is_priority_less_than_default(tinf::Node *cur_node) const {
  tinf::Edge edge_with_cur_node = {
    .from    = nullptr,
    .to      = cur_node,
    .from_at = nullptr
  };

  return get_priority(&edge_with_cur_node) < ComparatorByEdgePriorityRelativeToExpectedType::e_default_priority;
}

RestrictionLess::row RestrictionLess::parse_description(string const &description) {
  // Все description'ы состоят из трех колонок, разделенных между собой двумя пробелами (внутри колонки двух пробелов не должно быть)
  // Здесь происходит парсинг description'а на колонки по двум пробелам в качестве разделителя
  // Это нужно для динамичекого подсчета ширины колонок
  std::smatch matched;
  if (std::regex_match(description, matched, std::regex("(.+?)\\s\\s(.*?)(\\s\\s(.*))?"))) {
    return row(matched[1], matched[2], matched[4]);
  }
  return row("", "", description);
}

string RestrictionLess::get_actual_error_message() {
  tinf::ExprNode *as_expr_0 = nullptr;
  tinf::VarNode *as_var_0 = nullptr;
  tinf::VarNode *as_var_1 = nullptr;
  if (stacktrace.size() >= 2) {
    as_expr_0 = dynamic_cast<tinf::ExprNode *>(stacktrace[0]);
    as_var_0 = dynamic_cast<tinf::VarNode *>(stacktrace[0]);
    as_var_1 = dynamic_cast<tinf::VarNode *>(stacktrace[1]);
    if (as_expr_0 && as_expr_0->get_expr()->type() == op_instance_prop &&
        as_var_1 && as_var_1->is_variable()) {
      return string("Incorrect type of the following class field: ") + TermStringFormat::add_text_attribute(as_var_1->get_var_name(), TermStringFormat::bold, false) + "\n";
    }
  }
  if (!stacktrace.empty()) {
    if (as_var_0 && !as_var_0->is_variable() && !as_var_0->is_return_value_from_function()) {
      return string("Incorrect type of the ") + TermStringFormat::add_text_attribute(as_var_0->get_var_as_argument_name(), TermStringFormat::bold, false) + " at " + as_var_0->get_function_name() + "\n";
    }
  }
  if (stacktrace.size() >= 3 && as_expr_0 && as_var_1 &&
      dynamic_cast<tinf::TypeNode *>(stacktrace[2]) && dynamic_cast<tinf::TypeNode *>(stacktrace[2])->type_->ptype() == tp_var) {
    return TermStringFormat::paint("Unexpected conversion to var one of the arguments of the following function:\n", TermStringFormat::red, false) + TermStringFormat::add_text_attribute(as_expr_0
                                                                                                                                                                                            ->get_location_text(), TermStringFormat::bold, false) + "\n";
  }
  return "Mismatch of types\n";
}

string RestrictionLess::get_stacktrace_text() {
  // Делаем красивое форматирование stacktrace'а:
  // 1) Динамически считаем ширину его колонок, выводим выровненно
  // 2) Удаляем некоторые бесполезные дубликаты строчек
  vector<row> rows;
  for (int i = 0; i < stacktrace.size(); ++i) {
    row cur = parse_description(stacktrace[i]->get_description());
    row next = (i != int(stacktrace.size() - 1) ? parse_description(stacktrace[i + 1]->get_description()) : row());
    if (cur.col[0] == "as expression:" && next.col[0] == "as variable:" && cur.col[1] == next.col[1]) {
      i++;
    }
    rows.push_back(cur);
  }
  auto ith_row = [&](int idx) -> row { return (idx < rows.size() ? rows[idx] : row()); };

  // Если это ошибка несовпадения phpdoc у переменной инстанса, то первым в стектрейсе идёт ->var_name, убираем
  if (vk::string_view{rows[0].col[1]}.starts_with("->")) {
    auto as_expr_0 = dynamic_cast<tinf::ExprNode *>(stacktrace[0]);
    if (as_expr_0 && as_expr_0->get_expr()->type() == op_instance_prop) {
      rows.erase(rows.begin());
    }
  }
  // Удаление дубликатов при вызове статически отнаследованных функций (2 случая)
  // 1) Дублирование аргумента, в котором произошла ошибка:
  //  $x                                        at .../VK/A.php: VK\D :: demo (inherited from VK\A) : 20
  //  0-th arg ($x)                             at static function: VK\D :: demo (inherited from VK\A)
  //  $x                                        at .../VK/A.php: VK\D :: demo : 20
  //  0-th arg ($x)                             at static function: VK\D :: demo
  //
  // 2) Дубирование return'а:
  //  VK\B :: calc(...) (inherited from VK\A)   at .../dev.php: src_dev3b832f7b\u : 12
  //  return ...                                at static function: VK\B :: calc
  //  VK\B :: calc(...) (inherited from VK\A)   at .../VK/A.php: VK\B :: calc : -1
  //  return ...                                at static function: VK\B :: calc (inherited from VK\A)
  for (int i = 0; i < rows.size(); ++i) {
    if (ith_row(i).col[0] == "as expression:" && ith_row(i + 1).col[0] == "as argument:" &&
        ith_row(i + 2).col[0] == "as expression:" && ith_row(i + 3).col[0] == "as argument:" &&
        ith_row(i).col[1] == ith_row(i + 2).col[1] && ith_row(i + 1).col[1] == ith_row(i + 3).col[1] &&
        vk::string_view(ith_row(i + 1).col[2]).starts_with("at static function: ") &&
        vk::string_view(ith_row(i + 1).col[2]).starts_with(ith_row(i + 3).col[2]) &&
        ith_row(i + 1).col[2].substr(ith_row(i + 3).col[2].length(), 17) == " (inherited from ") {
      rows.erase(rows.begin() + i + 2, rows.begin() + i + 4);
    } else if (ith_row(i + 1).col[1] == "return ..." && ith_row(i + 3).col[1] == "return ..." &&
               vk::string_view(ith_row(i + 3).col[2]).starts_with("at static function: ") &&
               vk::string_view(ith_row(i + 3).col[2]).starts_with(ith_row(i + 1).col[2]) &&
               ith_row(i + 3).col[2].substr(ith_row(i + 1).col[2].length(), 17) == " (inherited from ") {
      std::smatch matched;
      string sanitized_col_i_1 = std::regex_replace(ith_row(i).col[1], std::regex(R"([\\\(\)\[\]])"), R"(\$&)");
      auto temp_string = ith_row(i + 2).col[1];
      if (std::regex_match(temp_string, matched, std::regex(sanitized_col_i_1 + " (\\(inherited from .+?\\))"))) {
        rows[i].col[1] += " " + matched[1].str();
        rows.erase(rows.begin() + i + 2, rows.begin() + i + 4);
      }
    }
  }
  int width[3] = {10, 15, 30};
  for (auto &row : rows) {
    width[0] = std::max(width[0], (int)row.col[0].length());
    width[1] = std::max(width[1], (int)row.col[1].length() - TermStringFormat::get_length_without_symbols(row.col[1]));
    width[2] = std::max(width[2], (int)row.col[2].length());
  }
  stringstream ss;
  for (auto &row : rows) {
    ss << std::setw(width[1] + 3 + TermStringFormat::get_length_without_symbols(row.col[1])) << std::left << row.col[1];
    ss << std::setw(width[2]) << std::left << row.col[2] << std::endl;
  }
  return ss.str();
}

bool RestrictionLess::check_broken_restriction_impl() {
  const TypeData *actual_type = actual_->get_type();
  const TypeData *expected_type = expected_->get_type();

  if (is_less(actual_type, expected_type)) {
    find_call_trace_with_error(actual_);
    desc = TermStringFormat::add_text_attribute("\n+----------------------+\n| TYPE INFERENCE ERROR |\n+----------------------+\n", TermStringFormat::bold);
    desc += TermStringFormat::paint(get_actual_error_message(), TermStringFormat::red);
    desc += "Expected type:\t" + colored_type_out(expected_type) + "\nActual type:\t" + colored_type_out(actual_type) + "\n";
    desc += TermStringFormat::add_text_attribute("+-------------+\n| STACKTRACE: |\n+-------------+", TermStringFormat::bold);
    desc += "\n";
    desc += get_stacktrace_text();
    return true;
  }

  return false;
}
