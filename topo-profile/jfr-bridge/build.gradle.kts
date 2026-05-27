// topo-profile-jvm-jfr — `.jfr` -> NDJSON bridge.
//
// Produces a self-contained jar that reads a `.jfr` recording (jdk.jfr binary
// format) through `jdk.jfr.consumer.RecordingFile` and emits one NDJSON line
// per event of interest on stdout. The NDJSON shape is identical to the
// existing `topo-lang-java/topo-profile/test/fixtures/jfr_sample.ndjson`
// fixture, so downstream the C++ `JfrNdjsonConverter` consumes either source
// without branching.
//
// `jdk.jfr.consumer.*` ships in the JDK as part of the `jdk.jfr` module
// (JDK 11+). Compile-time module access is forwarded explicitly so the
// classpath-build path resolves the symbols.

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

tasks.compileJava {
    options.compilerArgs.addAll(listOf("--add-modules", "jdk.jfr"))
}

application {
    mainClass.set("dev.topo.profile.jfr.Main")
}

tasks.jar {
    manifest {
        attributes["Main-Class"] = "dev.topo.profile.jfr.Main"
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
}
