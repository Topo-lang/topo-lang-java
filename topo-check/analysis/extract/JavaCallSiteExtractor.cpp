// JavaCallSiteExtractor — Extract external API call sites from Java source files.
//
// Strategy: regex-based line scanning with scope tracking.
// Tracks package name, class stack (nested classes), and current method.
// Handles Java 13+ text blocks ("""), block comments, and string literals.
// Matches Java-specific API call patterns including reflection, JNDI,
// MethodHandles, ServiceLoader, ScriptEngine, and other escape mechanisms.
// Builds qualified caller names as package::Class::method.

#include "JavaCallSiteExtractor.h"
#include "JavaUnsafeCatalog.h"
#include "topo/Check/CapabilityCatalog.h"

#include <fstream>
#include <regex>
#include <stack>
#include <string>
#include <vector>

namespace topo::check {

namespace {

/// Build a qualified name from package, class stack, and function name.
/// Uses "::" separator (Topo convention).
std::string buildQualified(const std::string& packageName,
                           const std::stack<std::string>& classStack,
                           const std::string& funcName) {
    std::vector<std::string> parts;

    // Package parts
    if (!packageName.empty()) {
        parts.push_back(packageName);
    }

    // Class stack (bottom to top)
    {
        std::stack<std::string> tmp = classStack;
        std::vector<std::string> classes;
        while (!tmp.empty()) {
            classes.push_back(tmp.top());
            tmp.pop();
        }
        for (auto it = classes.rbegin(); it != classes.rend(); ++it) {
            parts.push_back(*it);
        }
    }

    // Function name
    parts.push_back(funcName);

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += "::";
        result += parts[i];
    }
    return result;
}

/// Check if a line is a single-line comment
bool isCommentLine(const std::string& line) {
    size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) return false;
    return (line.size() > pos + 1 && line[pos] == '/' && line[pos + 1] == '/');
}

} // anonymous namespace

std::vector<DetectedCallSite> JavaCallSiteExtractor::extractCallSites(const std::string& filePath) {
    std::vector<DetectedCallSite> results;
    std::ifstream file(filePath);
    if (!file.is_open()) return results;

    // Scope tracking
    std::string packageName;
    std::stack<std::string> classStack;
    std::stack<int> classDepths;

    int braceDepth = 0;
    bool inFunction = false;
    int functionDepth = -1;
    std::string currentFunction;

    // Block comment and text block state
    bool inBlockComment = false;
    bool inTextBlock = false;

    // Regex patterns
    std::regex packageRegex(R"(^\s*package\s+([\w.]+)\s*;)");
    std::regex classRegex(
        R"(^\s*(?:(?:public|private|protected|static|abstract|final|strictfp)\s+)*(?:class|interface|enum)\s+(\w+))");
    std::regex funcDefRegex(
        R"(^\s*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default)\s+)*(?:\w[\w<>\[\],\s]*?)\s+(\w+)\s*\()");
    std::regex constructorRegex(R"(^\s*(?:(?:public|private|protected)\s+)?(\w+)\s*\()");

    // Java API call patterns — matches common external API usage
    // Handles FQN forms: new java.lang.ProcessBuilder(
    std::regex apiCallRegex(
        R"(\b(?:new\s+)?(?:[\w.]+\.)?(Socket|ServerSocket|DatagramSocket|URL|HttpURLConnection|URLConnection)"
        R"(|FileWriter|FileReader|FileInputStream|FileOutputStream)"
        R"(|BufferedReader|BufferedWriter|PrintWriter)"
        R"(|File|RandomAccessFile|FileChannel)"
        R"(|ProcessBuilder|Process)"
        R"(|ClassLoader)\s*\()");

    // Qualified API calls: Runtime.exec, Runtime.getRuntime, Class.forName, etc.
    std::regex qualifiedApiRegex(
        R"(\b(Runtime\.exec|Runtime\.getRuntime|Class\.forName|ClassLoader\.loadClass)\s*\()");

    // --- New escape pattern regexes ---

    // MethodHandles API (reflection alternative)
    static const std::regex methodHandlesRegex(R"(\bMethodHandles\.(lookup|privateLookupIn)\s*\()");
    static const std::regex methodHandleFindRegex(R"(\b(findVirtual|findStatic|findConstructor|findGetter|findSetter|unreflect)\s*\()");
    // ServiceLoader (dynamic class loading)
    static const std::regex serviceLoaderRegex(R"(\bServiceLoader\.load\s*\()");
    // JNDI (remote code execution)
    static const std::regex jndiRegex(R"(\b(InitialContext|InitialDirContext)\s*\.\s*(lookup|search)\s*\()");
    static const std::regex jndiConstructorRegex(R"(\bnew\s+(InitialContext|InitialDirContext)\s*\()");
    // URLClassLoader (arbitrary URL class loading)
    static const std::regex urlClassLoaderRegex(R"(\bnew\s+URLClassLoader\s*\()");
    // defineClass (raw bytecode injection)
    static const std::regex defineClassRegex(R"(\bdefineClass\s*\()");
    // XMLDecoder (deserialization RCE)
    static const std::regex xmlDecoderRegex(R"(\bnew\s+XMLDecoder\s*\()");
    // ScriptEngine (arbitrary code execution)
    static const std::regex scriptEngineRegex(R"(\b(ScriptEngine|ScriptEngineManager)\b)");
    // Runtime.load/loadLibrary (JNI via Runtime)
    static const std::regex runtimeLoadRegex(R"(\bRuntime\b.*\b(load|loadLibrary)\s*\()");
    // Method references to dangerous APIs
    static const std::regex dangerousMethodRefRegex(R"(\b(Class\s*::\s*forName|ProcessBuilder\s*::\s*new|Runtime[^;]*::\s*exec)\b)");
    // NIO Channels
    static const std::regex nioChannelRegex(R"(\b(FileChannel|SocketChannel|AsynchronousFileChannel|AsynchronousSocketChannel)\s*\.\s*open\s*\()");
    // ----- Issue #11: concurrency / exit / security / bytecode manipulation -----
    // Deprecated thread control: Thread.stop/suspend/resume/destroy.
    // To avoid false positives on every .stop()/.resume() call, only match patterns
    // where the receiver name looks like "Thread" or ends with "Thread" (e.g. workerThread).
    static const std::regex threadStopRegex(
        R"(\b(?:[\w]*Thread)\s*\.\s*(stop|suspend|resume|destroy)\s*\()");
    // Direct System.exit / Runtime exit/halt / addShutdownHook
    static const std::regex jvmExitRegex(
        R"(\b(System\.exit|Runtime\.getRuntime\(\)\.exit|Runtime\.getRuntime\(\)\.halt|Runtime\.getRuntime\(\)\.addShutdownHook)\s*\()");
    // Indirect Runtime.<verb> via local variable: <var>.halt(, <var>.exit(, <var>.addShutdownHook(
    // The L1 path cannot resolve the type, but if Runtime.getRuntime() is in scope this is the
    // strongest local heuristic. Always treat halt/exit/addShutdownHook as escape regardless of
    // receiver name -- these names are vanishingly rare on non-Runtime types.
    static const std::regex jvmExitReceiverRegex(
        R"(\b(\w+)\s*\.\s*(halt|exit|addShutdownHook)\s*\(\s*\d*\s*\))");
    // sun.misc.Signal.handle / .raise
    static const std::regex sunMiscSignalRegex(
        R"(\b(?:sun\.misc\.)?Signal\s*\.\s*(handle|raise)\s*\()");
    // AccessController.doPrivileged
    static const std::regex accessControllerRegex(
        R"(\b(?:[\w.]+\.)?AccessController\s*\.\s*doPrivileged\s*\()");
    // System.setSecurityManager
    static const std::regex setSecurityManagerRegex(
        R"(\bSystem\s*\.\s*setSecurityManager\s*\()");
    // Policy.setPolicy
    static const std::regex policySetPolicyRegex(
        R"(\b(?:[\w.]+\.)?Policy\s*\.\s*setPolicy\s*\()");
    // Instrumentation.redefineClasses / retransformClasses (instance call via any receiver)
    static const std::regex instrumentationRegex(
        R"(\b\w+\s*\.\s*(redefineClasses|retransformClasses)\s*\()");
    // MethodHandles.Lookup.defineClass / defineHiddenClass / IMPL_LOOKUP
    static const std::regex methodHandlesDefineRegex(
        R"(\b(?:[\w.]+\.)?(?:MethodHandles\s*\.\s*Lookup|Lookup)\s*\.\s*(defineClass|defineHiddenClass|IMPL_LOOKUP)\b)");
    // Generic <var>.defineClass( or .defineHiddenClass( as a fallback for resolved Lookup vars.
    // Excluded -- already covered by existing defineClassRegex.
    // ByteBuddy fluent API: new ByteBuddy() or ByteBuddy() chain
    static const std::regex byteBuddyRegex(
        R"(\b(?:new\s+)?ByteBuddy\s*\()");
    // org.objectweb.asm.ClassWriter
    static const std::regex asmClassWriterRegex(
        R"(\b(?:new\s+)?(?:[\w.]+\.)?ClassWriter\s*\()");
    // javassist CtClass.toBytecode (instance call)
    static const std::regex javassistCtClassRegex(
        R"(\b\w+\s*\.\s*toBytecode\s*\(\s*\))");

    // Java keywords that look like function names
    static const std::vector<std::string> keywords = {
        "if",      "for", "while", "switch", "return", "class",  "interface",
        "enum",    "new", "throw", "catch",  "try",    "assert", "super",
        "this",    "do",  "else",  "finally","break",  "continue"};

    auto isKeyword = [&](const std::string& name) {
        for (const auto& kw : keywords) {
            if (name == kw) return true;
        }
        return false;
    };

    auto emitSite = [&](const std::string& caller, const std::string& callee,
                        std::optional<CapabilityKind> cap, UnsafeLevel level, int ln) {
        DetectedCallSite site;
        site.callerQualifiedName = caller;
        site.calleePattern = callee;
        site.capability = cap;
        site.unsafeLevel = level;
        site.file = filePath;
        site.line = ln;
        results.push_back(std::move(site));
    };

    // Scan a line for the new escape patterns
    auto scanEscapePatterns = [&](const std::string& scanLine, const std::string& caller, int ln) {
        // MethodHandles API
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, methodHandlesRegex)) {
                emitSite(caller, "MethodHandles." + m[1].str(), CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, methodHandleFindRegex)) {
                emitSite(caller, m[1].str(), CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        // ServiceLoader
        if (std::regex_search(scanLine, serviceLoaderRegex)) {
            emitSite(caller, "ServiceLoader.load", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
        }
        // JNDI
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, jndiRegex)) {
                emitSite(caller, m[1].str() + "." + m[2].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, jndiConstructorRegex)) {
                emitSite(caller, m[1].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // URLClassLoader
        if (std::regex_search(scanLine, urlClassLoaderRegex)) {
            emitSite(caller, "URLClassLoader", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
        }
        // defineClass
        if (std::regex_search(scanLine, defineClassRegex)) {
            emitSite(caller, "defineClass", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
        }
        // XMLDecoder
        if (std::regex_search(scanLine, xmlDecoderRegex)) {
            emitSite(caller, "XMLDecoder", std::nullopt, UnsafeLevel::Escape, ln);
        }
        // ScriptEngine
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, scriptEngineRegex)) {
                emitSite(caller, m[1].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // Runtime.load/loadLibrary
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, runtimeLoadRegex)) {
                emitSite(caller, "Runtime." + m[1].str(), CapabilityKind::DynamicLoad, UnsafeLevel::System, ln);
            }
        }
        // Dangerous method references
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, dangerousMethodRefRegex)) {
                emitSite(caller, "method-ref:" + m[1].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // NIO Channels
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, nioChannelRegex)) {
                emitSite(caller, m[1].str() + ".open", CapabilityKind::File, UnsafeLevel::System, ln);
            }
        }
        // ----- Issue #11: deprecated Thread control -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, threadStopRegex)) {
                emitSite(caller, "Thread." + m[1].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: JVM exit (direct form) -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, jvmExitRegex)) {
                std::string fullMatch = m[1].str();
                // Normalize "Runtime.getRuntime().halt" -> "Runtime.halt" for catalog consistency
                std::string callee;
                if (fullMatch == "System.exit") {
                    callee = "System.exit";
                } else if (fullMatch == "Runtime.getRuntime().exit") {
                    callee = "Runtime.exit";
                } else if (fullMatch == "Runtime.getRuntime().halt") {
                    callee = "Runtime.halt";
                } else if (fullMatch == "Runtime.getRuntime().addShutdownHook") {
                    callee = "Runtime.addShutdownHook";
                } else {
                    callee = fullMatch;
                }
                emitSite(caller, callee, CapabilityKind::Process, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: indirect Runtime.<verb> via local variable -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, jvmExitReceiverRegex)) {
                // The receiver name (m[1]) is unknown without binding info.
                // Treat .halt(N), .exit(N), .addShutdownHook() as Runtime.<verb> regardless of
                // receiver name -- these names are practically only defined on Runtime.
                emitSite(caller, "Runtime." + m[2].str(), CapabilityKind::Process, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: sun.misc.Signal.handle/raise -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, sunMiscSignalRegex)) {
                emitSite(caller, "sun.misc.Signal." + m[1].str(), std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: AccessController.doPrivileged -----
        {
            if (std::regex_search(scanLine, accessControllerRegex)) {
                emitSite(caller, "AccessController.doPrivileged", std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: System.setSecurityManager -----
        {
            if (std::regex_search(scanLine, setSecurityManagerRegex)) {
                emitSite(caller, "System.setSecurityManager", std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: Policy.setPolicy -----
        {
            if (std::regex_search(scanLine, policySetPolicyRegex)) {
                emitSite(caller, "Policy.setPolicy", std::nullopt, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: Instrumentation.redefineClasses / retransformClasses -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, instrumentationRegex)) {
                emitSite(caller, "Instrumentation." + m[1].str(), CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: MethodHandles.Lookup.defineClass / defineHiddenClass / IMPL_LOOKUP -----
        {
            std::smatch m;
            if (std::regex_search(scanLine, m, methodHandlesDefineRegex)) {
                emitSite(caller, "MethodHandles.Lookup." + m[1].str(), CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: ByteBuddy fluent API -----
        {
            if (std::regex_search(scanLine, byteBuddyRegex)) {
                emitSite(caller, "ByteBuddy", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: org.objectweb.asm.ClassWriter -----
        {
            if (std::regex_search(scanLine, asmClassWriterRegex)) {
                emitSite(caller, "ClassWriter", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
        // ----- Issue #11: javassist CtClass.toBytecode -----
        {
            if (std::regex_search(scanLine, javassistCtClassRegex)) {
                emitSite(caller, "CtClass.toBytecode", CapabilityKind::DynamicLoad, UnsafeLevel::Escape, ln);
            }
        }
    };

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        // Text block state machine — must be checked before any other processing
        if (inTextBlock) {
            auto closePos = line.find("\"\"\"");
            if (closePos != std::string::npos) {
                inTextBlock = false;
                // Process remainder of line after closing """
                line = line.substr(closePos + 3);
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
                // Fall through to process remainder
            } else {
                continue; // Still inside text block
            }
        }

        // Build effective line content (strip comments, strings, text blocks for brace tracking)
        std::string effective;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];

            if (inBlockComment) {
                if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            // Single-line comment
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                break;
            }

            // Block comment start
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                inBlockComment = true;
                ++i;
                continue;
            }

            // Text block detection — must come BEFORE single " handling
            if (c == '"' && i + 2 < line.size() && line[i + 1] == '"' && line[i + 2] == '"') {
                // Text block opening — look for closing """ on same line
                auto closePos = line.find("\"\"\"", i + 3);
                if (closePos != std::string::npos) {
                    // Same-line text block: skip content
                    i = closePos + 2; // will be incremented by loop
                    continue;
                }
                // Multi-line text block — process code before this position, enter text block state
                inTextBlock = true;
                break; // stop processing this line
            }

            // String literal (single " delimiter)
            if (c == '"') {
                ++i;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\') ++i; // skip escaped char
                    ++i;
                }
                continue;
            }

            // Character literal
            if (c == '\'') {
                ++i;
                while (i < line.size() && line[i] != '\'') {
                    if (line[i] == '\\') ++i;
                    ++i;
                }
                continue;
            }

            // Track braces
            if (c == '{') {
                ++braceDepth;
            } else if (c == '}') {
                --braceDepth;
                if (braceDepth < 0) braceDepth = 0;
                // Pop class scope
                if (!classDepths.empty() && braceDepth == classDepths.top()) {
                    classStack.pop();
                    classDepths.pop();
                }
                // End function scope
                if (inFunction && braceDepth == functionDepth) {
                    inFunction = false;
                    functionDepth = -1;
                    currentFunction.clear();
                }
            }

            effective += c;
        }

        // Skip fully commented/empty lines
        if (effective.empty()) continue;
        if (isCommentLine(effective)) continue;

        // Detect package
        std::smatch pkgMatch;
        if (packageName.empty() && std::regex_search(effective, pkgMatch, packageRegex)) {
            std::string raw = pkgMatch[1].str();
            for (auto& ch : raw) {
                if (ch == '.') ch = ':';
            }
            std::string normalized;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == ':' && i + 1 < raw.size() && raw[i + 1] == ':') {
                    normalized += "::";
                    ++i;
                } else if (raw[i] == ':') {
                    normalized += "::";
                } else {
                    normalized += raw[i];
                }
            }
            packageName = normalized;
            continue;
        }

        // Detect class/interface/enum
        std::smatch classMatch;
        if (!inFunction && std::regex_search(effective, classMatch, classRegex)) {
            classStack.push(classMatch[1].str());
            classDepths.push(braceDepth - 1);
            continue;
        }

        // Detect method definition (only at class scope, not inside a method)
        if (!inFunction) {
            std::smatch funcMatch;
            if (std::regex_search(effective, funcMatch, funcDefRegex)) {
                std::string fname = funcMatch[1].str();
                if (!isKeyword(fname)) {
                    bool isConstructor = !classStack.empty() && fname == classStack.top();
                    if (!isConstructor && effective.find('{') != std::string::npos) {
                        inFunction = true;
                        functionDepth = braceDepth - 1;
                        currentFunction = fname;
                    } else if (!isConstructor) {
                        currentFunction = fname;
                    }
                }
            } else if (!classStack.empty()) {
                std::smatch ctorMatch;
                if (std::regex_search(effective, ctorMatch, constructorRegex)) {
                    std::string ctorName = ctorMatch[1].str();
                    if (!isKeyword(ctorName) && ctorName == classStack.top()) {
                        if (effective.find('{') != std::string::npos) {
                            inFunction = true;
                            functionDepth = braceDepth - 1;
                            currentFunction = ctorName;
                        }
                    }
                }
            }

            // Handle method body starting on next line
            if (!inFunction && !currentFunction.empty() && effective.find('{') != std::string::npos) {
                inFunction = true;
                functionDepth = braceDepth - 1;
            }
        }

        // Inside a method body: scan for API calls and escape patterns
        if (inFunction && braceDepth > functionDepth) {
            std::string callerName = buildQualified(packageName, classStack, currentFunction);

            // Check constructor/factory API calls: new Socket(, FileWriter(, etc.
            std::smatch apiMatch;
            std::string searchLine = effective;
            while (std::regex_search(searchLine, apiMatch, apiCallRegex)) {
                std::string callee = apiMatch[1].str();
                std::optional<CapabilityKind> cap;

                if (callee == "Socket" || callee == "ServerSocket" ||
                    callee == "DatagramSocket" || callee == "URL" ||
                    callee == "HttpURLConnection" || callee == "URLConnection") {
                    cap = CapabilityKind::Network;
                } else if (callee == "FileWriter" || callee == "FileReader" ||
                           callee == "FileInputStream" || callee == "FileOutputStream" ||
                           callee == "BufferedReader" || callee == "BufferedWriter" ||
                           callee == "PrintWriter" || callee == "File" ||
                           callee == "RandomAccessFile" || callee == "FileChannel") {
                    cap = CapabilityKind::File;
                } else if (callee == "ProcessBuilder" || callee == "Process") {
                    cap = CapabilityKind::Process;
                } else if (callee == "ClassLoader") {
                    cap = CapabilityKind::DynamicLoad;
                }

                auto level = JavaUnsafeCatalog::classifyCall(callee);
                if (level != UnsafeLevel::Safe || cap) {
                    emitSite(callerName, callee, cap,
                             (level != UnsafeLevel::Safe) ? level : UnsafeLevel::System, lineNum);
                }
                searchLine = apiMatch.suffix().str();
            }

            // Check qualified API calls: Runtime.exec(, Class.forName(, etc.
            searchLine = effective;
            while (std::regex_search(searchLine, apiMatch, qualifiedApiRegex)) {
                std::string callee = apiMatch[1].str();
                std::optional<CapabilityKind> cap;

                if (callee == "Runtime.exec" || callee == "Runtime.getRuntime") {
                    cap = CapabilityKind::Process;
                } else if (callee == "Class.forName" || callee == "ClassLoader.loadClass") {
                    cap = CapabilityKind::DynamicLoad;
                }

                auto level = JavaUnsafeCatalog::classifyCall(callee);
                if (level != UnsafeLevel::Safe || cap) {
                    emitSite(callerName, callee, cap,
                             (level != UnsafeLevel::Safe) ? level : UnsafeLevel::System, lineNum);
                }
                searchLine = apiMatch.suffix().str();
            }

            // Scan for new escape patterns
            scanEscapePatterns(effective, callerName, lineNum);
        }
    }

    return results;
}

} // namespace topo::check
