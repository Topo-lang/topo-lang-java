#include "JavaPlugin.h"

#include "JavaCheckRunner.h"
#include "JavaEmitter.h"
#include "JdtBridge.h"
#include "JavaAnalysisProvider.h"

namespace topo::lang {

// -----------------------------------------------------------------------
// EmitterFactory
// -----------------------------------------------------------------------

class JavaPlugin::JavaEmitterFactory : public EmitterFactory {
public:
    std::unique_ptr<transpile::Emitter> createEmitter() override {
        return std::make_unique<transpile::JavaEmitter>();
    }
    std::string fileExtension() const override { return ".java"; }
};

// -----------------------------------------------------------------------
// BuildDriverFactory
// -----------------------------------------------------------------------

class JavaPlugin::JavaBuildDriverFactory : public BuildDriverFactory {
public:
    std::string backendToolName() const override { return "topo-build-jvm-java"; }
    std::string extractorToolName() const override { return "topo-extract-java"; }
};

// -----------------------------------------------------------------------
// JavaPlugin
// -----------------------------------------------------------------------

JavaPlugin::JavaPlugin()
    : emitterFactory_(std::make_unique<JavaEmitterFactory>()),
      buildDriverFactory_(std::make_unique<JavaBuildDriverFactory>()) {}

HostLanguage JavaPlugin::language() const { return HostLanguage::Java; }

std::unique_ptr<check::LanguageAnalysisProvider> JavaPlugin::createAnalysisProvider() {
    return check::createJavaAnalysisProvider();
}

EmitterFactory* JavaPlugin::emitterFactory() { return emitterFactory_.get(); }
BuildDriverFactory* JavaPlugin::buildDriverFactory() { return buildDriverFactory_.get(); }
InitTemplateProvider* JavaPlugin::initTemplateProvider() { return &initProvider_; }

std::unique_ptr<lsp::LSPBridge> JavaPlugin::createLSPBridge() {
    return std::make_unique<lsp::JdtBridge>();
}

std::unique_ptr<CheckRunnerBase> JavaPlugin::createCheckRunner() {
    return std::make_unique<JavaCheckRunner>();
}

void registerJavaPlugin() {
    registerPlugin(std::make_unique<JavaPlugin>());
}

} // namespace topo::lang
