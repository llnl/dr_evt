DR_EVT coding style mostly follows the standard C++ libraries and Boost projects.

+ overriding style is lowercase separated with underbar
+ member fields: `m_*`
+ function names: lowercase with underbar
+ class names: start with uppercase (camel case after that e.g. DataType)
+ templated types: uppercase
  - derived typedef types:  `using value_type = SomeType::value_type_t`
+ header preprocessor guard: `DR_EVT_<NAMESPACE_PATH_NAME>_HPP`
  - use uppercase path components separated by underscores, for example
    `DR_EVT_TRACE_TRACE_HPP` for `src/trace/trace.hpp`
  - do not begin or end a guard with a double underscore; those identifiers
    are reserved to the implementation
+ indentation by 4 spaces, no tab
+ comments:
  - doxygen:
    - `/// single line comment `
    - `/** multi-line comment */`
    - use Doxygen's `@todo` tag for work items that belong in generated
      documentation; attach it to the relevant declaration, for example
      `/// @todo Explain the remaining implementation work.`

  - inside of a function use `//`
  - outside use a doxygen comment
    - minimize blocks of `//` or `/* */` comments that are not picked up by doxygen
+ for variable names, spell out the full dictionary words as much as possible
  and refrain from using undocumented or unfamiliar acronyms
+ use const keyword when a variable is not supposed to be modified and a member
  function does not update any member state.
+ avoid relying on implicit type promotion or conversion,
  and use explicit casting such as static_cast
+ do not convert an unsigned type to a signed type unless it is algorithmically
  necessary
+ check and fix compiler warnings
+ a `using namespace xxx` directive is acceptable only in a small local
  scope and only for a narrow namespace where it materially improves
  readability (for example, a short function body using `std::chrono`)
  - never place a `using namespace` directive at global scope
  - never import a broad namespace such as `std`; qualify those symbols
  - prefer explicit qualification when the imported namespace is not local
    and narrow
+ keep substantial implementation details outside a class definition body
  - a one-line accessor, mutator, or other trivial method may be defined inline
  - a short method of a few lines is also acceptable when keeping it inline
    materially improves readability
