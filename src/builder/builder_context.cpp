#include "builder/builder_context.h"
#include "blocks/for_loop_finder.h"
#include "blocks/label_inserter.h"
#include "blocks/loop_finder.h"
#include "blocks/loop_roll.h"
#include "blocks/var_namer.h"
#include "builder/builder.h"
#include "builder/exceptions.h"
#include "util/tracer.h"
#include <algorithm>

namespace builder {
builder_context *builder_context::current_builder_context = nullptr;
void builder_context::add_stmt_to_current_block(block::stmt::Ptr s,
						bool check_for_conflicts) {
	if (current_label != "") {
		s->annotation = current_label;
		current_label = "";
	}
	if (!s->static_offset.is_empty() &&
	    is_visited_tag(s->static_offset) > 0) {

		throw LoopBackException(s->static_offset);
	}
	std::string tag_string = s->static_offset.stringify();
	if (memoized_tags->map.find(tag_string) != memoized_tags->map.end() &&
	    check_for_conflicts && bool_vector.size() == 0) {
		// This tag has been seen on some other execution. We can reuse.
		// First find the tag -

		block::stmt_block::Ptr parent = memoized_tags->map[tag_string];
		unsigned int i = 0;
		for (i = 0; i < parent->stmts.size(); i++) {
			if (parent->stmts[i]->static_offset == s->static_offset)
				break;
		}
		// Special case of stmt expr and if_stmt
		if (block::isa<block::expr_stmt>(s) &&
		    block::isa<block::if_stmt>(parent->stmts[i])) {
			block::if_stmt::Ptr p_stmt =
			    block::to<block::if_stmt>(parent->stmts[i]);
			block::expr_stmt::Ptr expr =
			    block::to<block::expr_stmt>(s);

			if (p_stmt->cond->is_same(expr->expr1))
				throw MemoizationException(s->static_offset,
							   parent, i);
		}

		if (parent->stmts[i]->is_same(s))
			throw MemoizationException(s->static_offset, parent, i);
	}
	visited_offsets.insert(s->static_offset.stringify());
	current_block_stmt->stmts.push_back(s);
}
tracer::tag
get_offset_in_function(void) {
	tracer::tag offset = tracer::get_offset_in_function_impl(builder_context::current_builder_context);
	return offset;
}
builder_context::~builder_context() {
	for (unsigned int i = 0; i < assume_variables.size(); i++) {
		delete assume_variables[i];
	}
}
bool builder_context::is_visited_tag(tracer::tag &new_tag) {
	if (visited_offsets.find(new_tag.stringify()) != visited_offsets.end())
		return true;
	return false;
}
void builder_context::erase_tag(tracer::tag &erase_tag) {
	visited_offsets.erase(erase_tag.stringify());
}
void builder_context::commit_uncommitted(void) {
	for (auto block_ptr : uncommitted_sequence) {
		block::expr_stmt::Ptr s = std::make_shared<block::expr_stmt>();
		assert(block::isa<block::expr>(block_ptr));
		s->static_offset = block_ptr->static_offset;
		s->expr1 = block::to<block::expr>(block_ptr);
		assert(current_block_stmt != nullptr);
		add_stmt_to_current_block(s);
	}
	uncommitted_sequence.clear();
}
void builder_context::remove_node_from_sequence(block::expr::Ptr e) {
	uncommitted_sequence.remove(e);
}
void builder_context::add_node_to_sequence(block::expr::Ptr e) {
	uncommitted_sequence.push_back(e);
}

bool get_next_bool_from_context(builder_context *context,
				block::expr::Ptr expr) {
	context->commit_uncommitted();
	if (context->bool_vector.size() == 0) {
		tracer::tag offset = expr->static_offset;
		throw OutOfBoolsException(offset);
	}
	bool ret_val = context->bool_vector.back();
	context->bool_vector.pop_back();
	return ret_val;
}

static void trim_ast_at_offset(block::stmt::Ptr ast, tracer::tag offset) {
	return;
	block::stmt_block::Ptr top_level_block =
	    block::to<block::stmt_block>(ast);
	std::vector<block::stmt::Ptr> &stmts = top_level_block->stmts;
	std::vector<block::stmt::Ptr> new_stmts;

	unsigned int i;
	for (i = 0; i < stmts.size(); i++) {
		if (stmts[i]->static_offset == offset)
			break;
	}
	for (i++; i < stmts.size(); i++) {
		new_stmts.push_back(stmts[i]);
	}
	top_level_block->stmts = new_stmts;
}

static std::pair<std::vector<block::stmt::Ptr>, std::vector<block::stmt::Ptr>>
trim_common_from_back(block::stmt::Ptr ast1, block::stmt::Ptr ast2) {
	std::vector<block::stmt::Ptr> trimmed_stmts;
	std::vector<block::stmt::Ptr> &ast1_stmts =
	    block::to<block::stmt_block>(ast1)->stmts;
	std::vector<block::stmt::Ptr> &ast2_stmts =
	    block::to<block::stmt_block>(ast2)->stmts;

	std::vector<block::stmt::Ptr> split_decls;

	if (ast1_stmts.size() > 0 && ast2_stmts.size() > 0) {
		while (1) {
			if (ast1_stmts.size() == 0 || ast2_stmts.size() == 0)
				break;
			if (!ast1_stmts.back()->is_same(ast2_stmts.back())) {
				// There is a special case where there could be
				// an if stmt with same body but different
				// conditions We handle that by splitting the
				// condition from the if stmt using a variable
				if (block::isa<block::if_stmt>(
					ast1_stmts.back()) &&
				    block::isa<block::if_stmt>(
					ast2_stmts.back())) {
					block::if_stmt::Ptr if1 =
					    block::to<block::if_stmt>(
						ast1_stmts.back());
					block::if_stmt::Ptr if2 =
					    block::to<block::if_stmt>(
						ast2_stmts.back());
					if (if1->needs_splitting(if2)) {
						ast1_stmts.pop_back();
						ast2_stmts.pop_back();

						block::var::Ptr cond_var =
						    std::make_shared<
							block::var>();
						cond_var->var_type =
						    type_extractor<
							int>::extract_type();
						cond_var->static_offset =
						    tracer::get_unique_tag();

						block::decl_stmt::Ptr
						    decl_stmt =
							std::make_shared<
							    block::decl_stmt>();
						decl_stmt->static_offset =
						    if1->static_offset;

						decl_stmt->decl_var = cond_var;
						decl_stmt->init_expr = nullptr;

						split_decls.push_back(decl_stmt);

						block::expr_stmt::Ptr stmt1 =
						    std::make_shared<
							block::expr_stmt>();
						stmt1->static_offset =
						    if1->static_offset;
						block::assign_expr::Ptr
						    assign1 = std::make_shared<
							block::assign_expr>();
						assign1->static_offset =
						    if1->static_offset;
						block::var_expr::Ptr varexpr1 =
						    std::make_shared<
							block::var_expr>();
						varexpr1->static_offset =
						    if1->static_offset;
						varexpr1->var1 = cond_var;
						assign1->var1 = varexpr1;
						assign1->expr1 = if1->cond;
						stmt1->expr1 = assign1;
						ast1_stmts.push_back(stmt1);

						block::expr_stmt::Ptr stmt2 =
						    std::make_shared<
							block::expr_stmt>();
						stmt2->static_offset =
						    if2->static_offset;
						block::assign_expr::Ptr
						    assign2 = std::make_shared<
							block::assign_expr>();
						assign2->static_offset =
						    if2->static_offset;
						block::var_expr::Ptr varexpr2 =
						    std::make_shared<
							block::var_expr>();
						varexpr2->static_offset =
						    if2->static_offset;
						varexpr2->var1 = cond_var;
						assign2->var1 = varexpr2;
						assign2->expr1 = if2->cond;
						stmt2->expr1 = assign2;
						ast2_stmts.push_back(stmt2);

						block::var_expr::Ptr varexpr3 =
						    std::make_shared<
							block::var_expr>();
						varexpr3->static_offset =
						    if2->static_offset;
						varexpr3->var1 = cond_var;
						if1->cond = varexpr3;
						trimmed_stmts.push_back(if1);
						continue;

					} else {
						break;
					}
				}
				break;
			}
			if (ast1_stmts.back()->static_offset.is_empty()) {
				// The only possibility is that these two are
				// goto statements. Gotos are same only if they
				// are going to the same label
				assert(block::isa<block::goto_stmt>(
				    ast1_stmts.back()));
				assert(block::isa<block::goto_stmt>(
				    ast2_stmts.back()));
				block::goto_stmt::Ptr gt1 =
				    block::to<block::goto_stmt>(
					ast1_stmts.back());
				block::goto_stmt::Ptr gt2 =
				    block::to<block::goto_stmt>(
					ast2_stmts.back());
				if (gt1->temporary_label_number !=
				    gt2->temporary_label_number)
					break;
			}
			block::stmt::Ptr trimmed_stmt = ast1_stmts.back();
			ast1_stmts.pop_back();
			ast2_stmts.pop_back();
			trimmed_stmts.push_back(trimmed_stmt);
		}
	}
	// Handle a special case where one of the branch ends in a goto
	// In this case everything from the second branch can be safely added to
	// the common part This has to be checked only in the end because gotos
	// can appear on both the sides and should be trimmed of before

	if (ast1_stmts.size() != 0 && ast2_stmts.size() != 0) {
		if (block::isa<block::goto_stmt>(ast1_stmts.back())) {
			while (ast2_stmts.size() > 0) {
				block::stmt::Ptr trimmed_stmt =
				    ast2_stmts.back();
				ast2_stmts.pop_back();
				trimmed_stmts.push_back(trimmed_stmt);
			}
		} else if (block::isa<block::goto_stmt>(ast2_stmts.back())) {
			while (ast1_stmts.size() > 0) {
				block::stmt::Ptr trimmed_stmt =
				    ast1_stmts.back();
				ast1_stmts.pop_back();
				trimmed_stmts.push_back(trimmed_stmt);
			}
		}
	}
	std::reverse(trimmed_stmts.begin(), trimmed_stmts.end());
	return {trimmed_stmts, split_decls};
}
block::stmt::Ptr
builder_context::extract_ast_from_lambda(std::function<void(void)> lambda) {
	internal_stored_lambda = lambda;
	return extract_ast_from_function_impl();
}
block::stmt::Ptr
builder_context::extract_ast_from_function_impl(void) {
	std::vector<bool> b;
	block::stmt::Ptr ast = extract_ast_from_function_internal(b);

	block::var_namer namer;
	namer.ast = ast;
	ast->accept(&namer);

	block::label_collector collector;
	ast->accept(&collector);

	block::label_creator creator;
	creator.collected_labels = collector.collected_labels;
	ast->accept(&creator);

	block::label_inserter inserter;
	inserter.offset_to_label = creator.offset_to_label;
	ast->accept(&inserter);

	block::loop_finder finder;
	finder.ast = ast;
	ast->accept(&finder);

	block::for_loop_finder for_finder;
	for_finder.ast = ast;
	ast->accept(&for_finder);

	block::loop_roll_finder loop_roll_finder;
	ast->accept(&loop_roll_finder);

	return ast;
}
block::stmt::Ptr
builder_context::extract_ast_from_function_internal(std::vector<bool> b) {

	current_block_stmt = std::make_shared<block::stmt_block>();
	current_block_stmt->static_offset.clear();
	assert(current_block_stmt != nullptr);
	ast = current_block_stmt;
	bool_vector = b;


	block::stmt::Ptr ret_ast;
	try {
		current_builder_context = this;
		//function();
		lambda_wrapper();
                commit_uncommitted();
		ret_ast = ast;
		current_builder_context = nullptr;

	} catch (OutOfBoolsException &e) {

		current_builder_context = nullptr;

		block::expr_stmt::Ptr last_stmt = block::to<block::expr_stmt>(
		    current_block_stmt->stmts.back());
		current_block_stmt->stmts.pop_back();
		// erase_tag(e.static_offset);

		block::expr::Ptr cond_expr = last_stmt->expr1;

		builder_context true_context(memoized_tags);
		true_context.visited_offsets = visited_offsets;
		true_context.internal_stored_lambda = internal_stored_lambda;
		std::vector<bool> true_bv;
		true_bv.push_back(true);
		std::copy(b.begin(), b.end(), std::back_inserter(true_bv));
		block::stmt_block::Ptr true_ast = block::to<block::stmt_block>(
		    true_context.extract_ast_from_function_internal(true_bv));

		builder_context false_context(memoized_tags);
		false_context.visited_offsets = visited_offsets;
		false_context.internal_stored_lambda = internal_stored_lambda;
		std::vector<bool> false_bv;
		false_bv.push_back(false);
		std::copy(b.begin(), b.end(), std::back_inserter(false_bv));
		block::stmt_block::Ptr false_ast = block::to<block::stmt_block>(
		    false_context.extract_ast_from_function_internal(false_bv));

		trim_ast_at_offset(true_ast, e.static_offset);
		trim_ast_at_offset(false_ast, e.static_offset);

		std::pair<std::vector<block::stmt::Ptr>,
			  std::vector<block::stmt::Ptr>>
		    trim_pair = trim_common_from_back(true_ast, false_ast);

		std::vector<block::stmt::Ptr> trimmed_stmts = trim_pair.first;
		std::vector<block::stmt::Ptr> split_decls = trim_pair.second;

		erase_tag(e.static_offset);

		block::if_stmt::Ptr new_if_stmt =
		    std::make_shared<block::if_stmt>();
		new_if_stmt->annotation = last_stmt->annotation;
		new_if_stmt->static_offset = e.static_offset;

		new_if_stmt->cond = cond_expr;
		new_if_stmt->then_stmt = true_ast;
		new_if_stmt->else_stmt = false_ast;

		for (auto stmt : split_decls)
			add_stmt_to_current_block(stmt, false);
		add_stmt_to_current_block(new_if_stmt, false);

		std::copy(trimmed_stmts.begin(), trimmed_stmts.end(),
			  std::back_inserter(current_block_stmt->stmts));
		ret_ast = ast;
	} catch (LoopBackException &e) {
		current_builder_context = nullptr;

		block::goto_stmt::Ptr goto_stmt =
		    std::make_shared<block::goto_stmt>();
		goto_stmt->static_offset.clear();
		goto_stmt->temporary_label_number = e.static_offset;

		add_stmt_to_current_block(goto_stmt, false);
		ret_ast = ast;
	} catch (MemoizationException &e) {
		for (unsigned int i = e.child_id; i < e.parent->stmts.size();
		     i++) {
			add_stmt_to_current_block(e.parent->stmts[i], false);
		}
		ret_ast = ast;
	}

	// Update the memoized table with the stmt block we just created
	for (unsigned int i = 0; i < current_block_stmt->stmts.size(); i++) {
		block::stmt::Ptr s = current_block_stmt->stmts[i];
		// If any of the statements are if conditions, remove the
		// internal statements from the table
		if (block::isa<block::if_stmt>(s)) {
			block::if_stmt::Ptr if1 = block::to<block::if_stmt>(s);
			assert(block::isa<block::stmt_block>(if1->then_stmt));
			assert(block::isa<block::stmt_block>(if1->else_stmt));
			for (auto &stmt :
			     block::to<block::stmt_block>(if1->then_stmt)
				 ->stmts) {
				auto it = memoized_tags->map.find(
				    stmt->static_offset.stringify());
				if (it != memoized_tags->map.end())
					memoized_tags->map.erase(it);
			}
			for (auto &stmt :
			     block::to<block::stmt_block>(if1->else_stmt)
				 ->stmts) {
				auto it = memoized_tags->map.find(
				    stmt->static_offset.stringify());
				if (it != memoized_tags->map.end())
					memoized_tags->map.erase(it);
			}
		}
		memoized_tags->map[s->static_offset.stringify()] =
		    current_block_stmt;
	}

	ast = current_block_stmt = nullptr;
	return ret_ast;
}

void lambda_wrapper_impl(void) {
	builder_context::current_builder_context->internal_stored_lambda();
}

} // namespace builder
