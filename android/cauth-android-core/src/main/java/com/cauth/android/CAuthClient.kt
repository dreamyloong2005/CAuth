package com.cauth.android

import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong

enum class CAuthSessionStorageKind(val nativeValue: Int) {
    Default(0),
    Memory(1),
    FilePath(2),
    SecureStorage(3),
}

data class CAuthClientOptions(
    val sessionStorageKind: CAuthSessionStorageKind = CAuthSessionStorageKind.Default,
    val sessionStoragePath: String? = null,
    val sessionStorageNamespace: String? = null,
    val sessionStorageKey: String? = null,
)

class CAuthClient private constructor(
    private val handleRef: AtomicLong,
) : Closeable {
    companion object {
        fun create(options: CAuthClientOptions = CAuthClientOptions()): CAuthClient = CAuthClient(
            AtomicLong(
                CAuthNativeCore.nativeCreateClientWithOptions(
                    sessionStorageKind = options.sessionStorageKind.nativeValue,
                    sessionStoragePath = options.sessionStoragePath,
                    sessionStorageNamespace = options.sessionStorageNamespace,
                    sessionStorageKey = options.sessionStorageKey,
                ),
            ),
        )
    }

    fun version(): String = CAuthNativeCore.nativeGetVersionString()

    override fun close() {
        val handle = handleRef.getAndSet(0)
        if (handle != 0L) {
            CAuthNativeCore.nativeDestroyClient(handle)
        }
    }

    fun requireNativeHandle(): Long {
        val handle = handleRef.get()
        check(handle != 0L) { "CAuthClient is already closed." }
        return handle
    }
}
