plugins {
    id("com.android.library")
    kotlin("android")
}

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

android {
    namespace = "com.cauth.android.steam.auth"
    compileSdk = 35

    defaultConfig {
        minSdk = 26

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DCAUTH_BUILD_CLI=OFF",
                    "-DCAUTH_BUILD_TESTS=OFF",
                    "-DCAUTH_BUILD_FFI=ON",
                )
            }
        }
    }

    buildFeatures {
        buildConfig = false
    }

    packaging {
        jniLibs {
            excludes += setOf(
                "**/libcauth_core_ffi.so",
                "**/libcauth_steam_depot_ffi.so",
                "**/libcauth_steam_cloud_ffi.so",
                "**/libcauth_android_core_jni.so",
                "**/libcauth_android_steam_depot_jni.so",
                "**/libcauth_android_steam_cloud_jni.so",
            )
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_17)
        }
    }
}

dependencies {
    implementation(project(":cauth-android-core"))
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
