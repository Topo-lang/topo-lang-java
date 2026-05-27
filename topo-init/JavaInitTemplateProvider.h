#ifndef TOPO_LANG_JAVA_INITTEMPLATEPROVIDER_H
#define TOPO_LANG_JAVA_INITTEMPLATEPROVIDER_H

#include "topo/Lang/InitTemplateProvider.h"

namespace topo::lang {

class JavaInitTemplateProvider : public InitTemplateProvider {
public:
    std::string languageName() const override { return "java"; }

    std::vector<std::string> filePatterns() const override {
        return {"*.java"};
    }

    std::string sourceFileGlob() const override { return "src/**/*.java"; }

    std::string generateTopoToml(const std::string& projectName) const override;
    std::string generateTypeBindings() const override;
};

} // namespace topo::lang

#endif // TOPO_LANG_JAVA_INITTEMPLATEPROVIDER_H
