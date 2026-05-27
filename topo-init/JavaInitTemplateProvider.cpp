#include "JavaInitTemplateProvider.h"

namespace topo::lang {

std::string JavaInitTemplateProvider::generateTopoToml(const std::string& projectName) const {
    return "[topo]\n"
           "root = \"" + projectName + ".topo\"\n"
           "\n"
           "[build]\n"
           "language = \"java\"\n"
           "sources = [\"src/**/*.java\"]\n"
           "output = \"" + projectName + "\"\n";
}

std::string JavaInitTemplateProvider::generateTypeBindings() const {
    // Java type bindings. Match the spelling of the working examples in
    // topo-lang-java/examples/quickstart and showcase (capitalised
    // Java-idiom names — Int, Boolean, etc. — aliased to the std::java
    // bridge namespace). `topo-init java` previously emitted the C++
    // std::cpp17 family verbatim from the copy-paste origin in
    // CppInitTemplateProvider, producing projects that did not type-check
    // through JavaSymbolExtractor / JavaEmitter.
    return "using Int = std::java::int;\n"
           "using Boolean = std::java::boolean;\n"
           "using Long = std::java::long;\n"
           "using Double = std::java::double;\n"
           "using String = std::java::String;\n";
}

} // namespace topo::lang
