#ifndef TOPO_LANG_JAVAPLUGIN_H
#define TOPO_LANG_JAVAPLUGIN_H

#include "topo/Lang/LanguagePlugin.h"
#include "topo/Lang/CheckRunnerBase.h"
#include "topo/Lang/EmitterFactory.h"
#include "topo/Lang/BuildDriverFactory.h"
#include "JavaInitTemplateProvider.h"

namespace topo::lang {

class JavaPlugin : public LanguagePlugin {
public:
    JavaPlugin();

    HostLanguage language() const override;
    std::unique_ptr<check::LanguageAnalysisProvider> createAnalysisProvider() override;
    EmitterFactory* emitterFactory() override;
    BuildDriverFactory* buildDriverFactory() override;
    InitTemplateProvider* initTemplateProvider() override;
    std::unique_ptr<lsp::LSPBridge> createLSPBridge() override;
    std::unique_ptr<CheckRunnerBase> createCheckRunner() override;

private:
    class JavaEmitterFactory;
    class JavaBuildDriverFactory;
    std::unique_ptr<JavaEmitterFactory> emitterFactory_;
    std::unique_ptr<JavaBuildDriverFactory> buildDriverFactory_;
    JavaInitTemplateProvider initProvider_;
};

/// Call once at startup to register the Java plugin.
void registerJavaPlugin();

} // namespace topo::lang

#endif // TOPO_LANG_JAVAPLUGIN_H
