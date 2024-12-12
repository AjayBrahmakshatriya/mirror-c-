#ifndef BUILDER_CONTEXT
#define BUILDER_CONTEXT
#include "blocks/basic_blocks.h"
#include "blocks/dominance.h"
#include "blocks/expr.h"
#include "blocks/stmt.h"
#include "builder/forward_declarations.h"
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace builder {

template <typename T>
block::expr::Ptr create_foreign_expr(const T t);
template <typename T>
builder create_foreign_expr_builder(const T t);

class static_var_base;

class tracking_tuple {
public:
	const unsigned char *ptr;
	uint32_t size;
	static_var_base *var_ref;

	tracking_tuple(const unsigned char *_ptr, uint32_t _size, static_var_base *_var_ref)
	    : ptr(_ptr), size(_size), var_ref(_var_ref) {}

	std::string snapshot(void) {
		std::string output_string;
		char temp[4];
		for (unsigned int i = 0; i < size; i++) {
			sprintf(temp, "%02x", ptr[i]);
			output_string += temp;
		}
		return output_string;
	}
};

class tag_map {
public:
	std::unordered_map<std::string, block::stmt_block::Ptr> map;
};

void lambda_wrapper(std::function<void(void)>);
void lambda_wrapper_close(void);

void coroutine_wrapper(std::function<void(void)>);
void coroutine_wrapper_close(void);

class builder_context {
public:
	static builder_context *current_builder_context;
	static int debug_creation_counter;

	std::function<void(void)> internal_stored_lambda;

	std::list<block::block::Ptr> uncommitted_sequence;
	block::stmt::Ptr ast;
	block::stmt_block::Ptr current_block_stmt;

	std::vector<bool> bool_vector;
	std::unordered_set<std::string> visited_offsets;

	std::vector<block::expr::Ptr> expr_sequence;
	unsigned long long expr_counter = 0;

	tag_map _internal_tags;
	tag_map *memoized_tags;

	// Flags for controlling BuildIt extraction
	// and code generation behavior
	bool use_memoization = true;
	bool run_rce = false;
	bool feature_unstructured = false;
	bool dynamic_use_cxx = false;
	std::string dynamic_compiler_flags = "";
	std::string dynamic_header_includes = "";
	bool enable_d2x = false;

	bool is_visited_tag(tracer::tag &new_tag);
	void erase_tag(tracer::tag &erase_tag);

	builder_context(tag_map *_map = nullptr) {
		if (_map == nullptr) {
			memoized_tags = &_internal_tags;
		} else {
			memoized_tags = _map;
		}
		current_block_stmt = nullptr;
		ast = nullptr;

		debug_creation_counter++;
	}

	void commit_uncommitted(void);
	void remove_node_from_sequence(block::expr::Ptr);
	void add_node_to_sequence(block::expr::Ptr);

	void add_stmt_to_current_block(block::stmt::Ptr, bool check_for_conflicts = true);

	block::stmt::Ptr extract_ast_from_function(void (*f)(void)) {
		std::function<void(void)> l = f;
		return extract_ast_from_lambda(l);
	}
	block::stmt::Ptr extract_ast_from_lambda(std::function<void(void)>);
	block::stmt::Ptr extract_ast_from_function_impl(void);
	block::stmt::Ptr extract_ast_from_function_internal(std::vector<bool> bl = std::vector<bool>());

	block::func_decl::Ptr current_func_decl;
	template <typename F, typename... OtherArgs>
	block::stmt::Ptr extract_function_ast(F func_input, std::string func_name, OtherArgs &&...other_args) {
		current_func_decl = std::make_shared<block::func_decl>();
		current_func_decl->func_name = func_name;
		// The extract_signature_from_lambda will update the return type
		current_func_decl->body = extract_ast_from_lambda(
		    extract_signature_from_lambda<F, OtherArgs &...>::from(this, func_input, func_name, other_args...));
		return current_func_decl;
	}

	std::string current_label;

	std::vector<tracking_tuple> static_var_tuples;
	std::vector<tracking_tuple> deferred_static_var_tuples;

	std::vector<var *> assume_variables;

	template <typename T>
	T *assume_variable(std::string name) {
		T * new_asm_variable = new T(with_name(name));
		assume_variables.push_back(new_asm_variable);

		return new_asm_variable;
	}
	~builder_context();
};
bool get_next_bool_from_context(builder_context *context, block::expr::Ptr);
tracer::tag get_offset_in_function(void);

} // namespace builder

#endif
