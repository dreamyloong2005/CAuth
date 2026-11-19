plugins {
    id("com.android.library")
    kotlin("android")
}

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

extra["cauthMcppPackages"] = listOf("cauth.android-steam-depot-jni")
extra["cauthMcppLibraries"] = listOf(
    "libzstd.so",
    "libcauth_steam_depot.so",
    "libcauth_steam_depot_ffi.so",
    "libcauth_android_steam_depot_jni.so",
)
apply(from = "../gradle/mcpp-native.gradle")

android {
    namespace = "com.cauth.android.steam.depot"
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
                "**/libcauth_core_ffi.so",
                "**/libcauth_steam_auth_ffi.so",
                "**/libcauth_steam_cloud_ffi.so",
                "**/libcauth_android_core_jni.so",
                "**/libcauth_android_steam_auth_jni.so",
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
    implementation(project(":cauth-android-core"))
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
