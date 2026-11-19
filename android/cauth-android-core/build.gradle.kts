plugins {
    id("com.android.library")
    kotlin("android")
}

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

extra["cauthMcppPackages"] = listOf("cauth.android-core-jni")
extra["cauthMcppLibraries"] = listOf(
    "libcauth_core.so",
    "libcauth_core_ffi.so",
    "libcauth_android_core_jni.so",
)
extra["cauthMcppCopySharedStl"] = true
apply(from = "../gradle/mcpp-native.gradle")

android {
    namespace = "com.cauth.android"
    compileSdk = 35

    defaultConfig {
        minSdk = 26

    }

    buildFeatures {
        buildConfig = false
    }

    packaging {
        jniLibs {
            excludes += setOf(
                "**/libcauth_steam_auth_ffi.so",
                "**/libcauth_steam_depot_ffi.so",
                "**/libcauth_steam_cloud_ffi.so",
                "**/libcauth_android_steam_auth_jni.so",
                "**/libcauth_android_steam_depot_jni.so",
                "**/libcauth_android_steam_cloud_jni.so",
            )
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
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
}
