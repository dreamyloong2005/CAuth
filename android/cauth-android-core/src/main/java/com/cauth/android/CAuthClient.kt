package com.cauth.android

import java.io.Closeable
import java.util.concurrent.atomic.AtomicLong

class CAuthClient private constructor(
    private val handleRef: AtomicLong,
) : Closeable {
    companion object {
        fun create(): CAuthClient = CAuthClient(AtomicLong(CAuthNativeCore.nativeCreateClient()))
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
