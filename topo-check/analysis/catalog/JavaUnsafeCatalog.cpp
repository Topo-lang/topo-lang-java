#include "JavaUnsafeCatalog.h"

#include <unordered_set>

namespace topo::check {

UnsafeLevel JavaUnsafeCatalog::classifyCall(const std::string& pattern) {
    // Level 4: Language escape mechanisms
    static const std::unordered_set<std::string> escape = {
        "Class.forName", "ClassLoader.loadClass",
        "Method.invoke", "Field.set", "Field.get",
        "Constructor.newInstance",
        "ObjectInputStream", "ObjectOutputStream",
        "Runtime.exec", "ProcessBuilder",
        "Unsafe", "sun.misc.Unsafe",
        // MethodHandles API (reflection alternative)
        "MethodHandles.lookup", "MethodHandles.privateLookupIn",
        "findVirtual", "findStatic", "findConstructor",
        "findGetter", "findSetter", "unreflect",
        // ServiceLoader (dynamic class loading)
        "ServiceLoader.load",
        // JNDI (remote code execution via LDAP/RMI)
        "InitialContext.lookup", "InitialContext.search",
        "InitialDirContext.lookup", "InitialDirContext.search",
        "InitialContext", "InitialDirContext",
        // URLClassLoader (arbitrary URL class loading)
        "URLClassLoader",
        // defineClass (raw bytecode injection)
        "defineClass",
        // XMLDecoder (deserialization RCE)
        "XMLDecoder",
        // ScriptEngine (arbitrary code execution)
        "ScriptEngine", "ScriptEngineManager",
        // Method references to dangerous APIs
        "Class::forName", "ProcessBuilder::new", "Runtime::exec",
        // ----- Issue #11: concurrency / security / bytecode manipulation -----
        // Deprecated concurrency-breaking primitives
        "Thread.stop", "Thread.suspend", "Thread.resume", "Thread.destroy",
        // sun.misc.Signal (low-level signal handling)
        "sun.misc.Signal.handle", "sun.misc.Signal.raise",
        // JVM exit / shutdown bypassing hooks
        "System.exit", "Runtime.exit", "Runtime.halt",
        "Runtime.addShutdownHook",
        // SecurityManager / AccessController (deprecated but callable)
        "AccessController.doPrivileged",
        "System.setSecurityManager",
        "Policy.setPolicy",
        // Runtime bytecode manipulation via java.lang.instrument
        "Instrumentation.redefineClasses", "Instrumentation.retransformClasses",
        // MethodHandles.Lookup defineClass / hidden class / IMPL_LOOKUP backdoor
        "MethodHandles.Lookup.defineClass",
        "MethodHandles.Lookup.defineHiddenClass",
        "MethodHandles.Lookup.IMPL_LOOKUP",
        // Third-party bytecode manipulation libraries
        "ClassWriter",          // org.objectweb.asm.ClassWriter
        "ByteBuddy",            // net.bytebuddy.ByteBuddy
        "CtClass.toBytecode",   // javassist.CtClass
    };
    if (escape.count(pattern)) return UnsafeLevel::Escape;

    // Level 3: User input handling
    static const std::unordered_set<std::string> input = {
        "HttpServletRequest", "HttpServletResponse",
        "Statement.execute", "Statement.executeQuery", "Statement.executeUpdate",
        "PreparedStatement.execute",
    };
    if (input.count(pattern)) return UnsafeLevel::Input;

    // Level 1: System calls
    static const std::unordered_set<std::string> systemCalls = {
        "FileInputStream", "FileOutputStream", "FileReader", "FileWriter",
        "RandomAccessFile", "Files.read", "Files.write",
        "Files.copy", "Files.move", "Files.delete",
        "Files.createDirectory", "Files.createDirectories",
        "Files.newBufferedWriter", "Files.newBufferedReader",
        "Files.newInputStream", "Files.newOutputStream",
        "System.out.println", "System.out.print", "System.out.printf",
        "System.err.println", "System.err.print",
        "PrintStream.println", "PrintStream.print", "PrintStream.printf",
        "PrintWriter.println", "PrintWriter.print", "PrintWriter.printf",
        "Socket", "ServerSocket", "DatagramSocket",
        "URLConnection", "HttpURLConnection", "URL",
        // Runtime.load/loadLibrary (JNI via Runtime)
        "Runtime.load", "Runtime.loadLibrary",
        // NIO Channels
        "FileChannel", "SocketChannel",
        "AsynchronousFileChannel", "AsynchronousSocketChannel",
    };
    if (systemCalls.count(pattern)) return UnsafeLevel::System;

    return UnsafeLevel::Safe;
}

UnsafeLevel JavaUnsafeCatalog::classifyImport(const std::string& path) {
    // Level 4: Language escape mechanisms
    if (path.find("java.lang.invoke") != std::string::npos) return UnsafeLevel::Escape;
    if (path.find("javax.naming") != std::string::npos) return UnsafeLevel::Escape;
    if (path.find("javax.script") != std::string::npos) return UnsafeLevel::Escape;
    if (path.find("java.beans") != std::string::npos) return UnsafeLevel::Escape;
    if (path.find("java.lang.reflect") != std::string::npos) return UnsafeLevel::Escape;
    if (path.find("sun.misc") != std::string::npos) return UnsafeLevel::Escape;
    if (path == "java.util.ServiceLoader") return UnsafeLevel::Escape;

    // Level 3
    if (path.find("javax.servlet") != std::string::npos) return UnsafeLevel::Input;
    if (path.find("java.sql") != std::string::npos) return UnsafeLevel::Input;

    // Level 1
    if (path.find("java.io") != std::string::npos) return UnsafeLevel::System;
    if (path.find("java.nio") != std::string::npos) return UnsafeLevel::System;
    if (path.find("java.net") != std::string::npos) return UnsafeLevel::System;
    if (path.find("javax.net") != std::string::npos) return UnsafeLevel::System;
    if (path == "java.lang.ProcessBuilder" || path == "java.lang.Runtime")
        return UnsafeLevel::System;
    // java.lang star import covers ProcessBuilder and Runtime
    if (path == "java.lang") return UnsafeLevel::System;

    // Level 2: third-party (anything not java.* or javax.*)
    if (path.substr(0, 5) != "java." && path.substr(0, 6) != "javax.") {
        return UnsafeLevel::Dep;
    }

    return UnsafeLevel::Safe;
}

} // namespace topo::check
