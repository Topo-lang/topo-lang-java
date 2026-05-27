// topo-debug-java — JDWP-based Extract adapter.
//
// Produces a self-contained jar that speaks the topo-debug binary-frame wire
// protocol on stdout/stdin, driving a target Java class through the JDWP/JDI
// debug interface. Mirrors topo-debug-cpp's role but uses com.sun.jdi.* (ships
// with every JDK as the `jdk.jdi` module) instead of liblldb.

plugins {
    java
    application
}

group = "dev.topo"
version = "0.1.0"

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(17))
    }
}

repositories {
    mavenCentral()
}

dependencies {
    testImplementation("org.junit.jupiter:junit-jupiter:5.11.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

// JDI (com.sun.jdi.*) ships with the JDK as the `jdk.jdi` module. The Java
// language uses module-path resolution at compile time; we keep the
// compileJava task on the standard classpath so referenced `com.sun.jdi.*`
// types resolve via the JDK's bootstrap module set.
tasks.compileJava {
    options.compilerArgs.addAll(listOf("--add-modules", "jdk.jdi"))
}

tasks.test {
    useJUnitPlatform()
}

application {
    mainClass.set("dev.topo.debug.Main")
}

tasks.jar {
    manifest {
        attributes["Main-Class"] = "dev.topo.debug.Main"
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
}
