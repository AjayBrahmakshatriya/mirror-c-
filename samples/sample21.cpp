#include "blocks/c_code_generator.h"
#include "builder/builder.h"
#include "builder/builder_context.h"
#include "builder/static_var.h"
#include <iostream>

using builder::dyn_var;
using builder::static_var;

class dummy {
public:
	std::string value;

	// We have to define this so that the foreign_expr can check for
	// equality
	bool operator==(dummy &other) {
		if (value == other.value)
			return true;
		return false;
	}
};
// We add it in the name space to make sure that the template is properly
// specialized
namespace builder {
template <>
builder::builder(const dummy &t) {
	construct_builder_from_foreign_expr(t);
}
} // namespace builder

// A simple straight line code with 2 variable declarations and one operator
int main(int argc, char *argv[]) {
	builder::builder_context context;

	dummy foo;
	foo.value = "foo";

	auto ast = context.extract_ast_from_lambda([=] {
		dyn_var<int> x = 2;
		dyn_var<int> bar = foo + x;
	});

	ast->dump(std::cout, 0);
	return 0;
}
