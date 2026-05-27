plugins {
    `java-library`
}

group = "dev.topo"
version = "1.0.0"

java {
    // Pin to Java 25 (LTS). The runtime jar uses java.lang.foreign.Arena
    // (Panama FFM), which must not be compiled as a preview-marked class
    // file: preview-marked classes are rejected by later JDK majors (a
    // Java 21 preview class fails to load on Java 25), which broke Arena
    // loading in benchmarks. FFM is permanent (non-preview) since Java 22,
    // so any toolchain >= 22 is correct; 25 is chosen as the current LTS.
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(25))
    }
}

repositories {
    mavenCentral()
}

dependencies {
    testImplementation("org.junit.jupiter:junit-jupiter:5.11.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.test {
    useJUnitPlatform()
}

tasks.jar {
    archiveBaseName.set("topo-runtime")
    archiveVersion.set("")
}
