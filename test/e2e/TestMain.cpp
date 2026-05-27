#include "JavaPlugin.h"
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    topo::lang::registerJavaPlugin();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
