package com.cauth.android

import okhttp3.ConnectionSpec
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import javax.net.ssl.SSLHandshakeException
import okhttp3.TlsVersion

object CAuthAndroidHttpBridge {
    private const val CancelPollIntervalMs = 50L

    private fun buildClient(
        connectTimeoutMs: Int,
        readTimeoutMs: Int,
        connectionSpecs: List<ConnectionSpec>,
    ): OkHttpClient {
        val normalizedConnectTimeoutMs = connectTimeoutMs.coerceAtLeast(0).toLong()
        val normalizedReadTimeoutMs = readTimeoutMs.coerceAtLeast(0).toLong()
        return OkHttpClient.Builder()
            .connectTimeout(normalizedConnectTimeoutMs, TimeUnit.MILLISECONDS)
            .readTimeout(normalizedReadTimeoutMs, TimeUnit.MILLISECONDS)
            .writeTimeout(normalizedReadTimeoutMs, TimeUnit.MILLISECONDS)
            .callTimeout(0, TimeUnit.MILLISECONDS)
            .retryOnConnectionFailure(true)
            .followRedirects(true)
            .followSslRedirects(true)
            .protocols(listOf(Protocol.HTTP_1_1))
            .connectionSpecs(connectionSpecs)
            .build()
    }

    private val modernConnectionSpecs = listOf(
        ConnectionSpec.MODERN_TLS,
        ConnectionSpec.COMPATIBLE_TLS,
    )

    private val tls12OnlyConnectionSpecs = listOf(
        ConnectionSpec.Builder(ConnectionSpec.COMPATIBLE_TLS)
            .tlsVersions(TlsVersion.TLS_1_2)
            .allEnabledCipherSuites()
            .build(),
    )

    private fun executeRequest(
        client: OkHttpClient,
        request: Request,
    ): ByteArray {
        val call = client.newCall(request)
        val executor = Executors.newSingleThreadExecutor()
        val future: Future<ByteArray> = executor.submit<ByteArray> {
            call.execute().use { response ->
                if (!response.isSuccessful) {
                    throw IOException("HTTP ${response.code}")
                }
                response.body?.bytes() ?: ByteArray(0)
            }
        }
        try {
            while (true) {
                if (CAuthNativeCore.nativeIsOperationCanceled()) {
                    call.cancel()
                    future.cancel(true)
                    throw IOException("operation canceled")
                }
                try {
                    return future.get(CancelPollIntervalMs, TimeUnit.MILLISECONDS)
                } catch (_: TimeoutException) {
                    continue
                } catch (error: ExecutionException) {
                    val cause = error.cause
                    if (cause is IOException) {
                        throw cause
                    }
                    throw IOException("HTTP request failed", cause ?: error)
                }
            }
        } finally {
            executor.shutdownNow()
            client.dispatcher.cancelAll()
            client.dispatcher.executorService.shutdown()
            client.connectionPool.evictAll()
        }
    }

    @JvmStatic
    fun getText(url: String, timeoutMs: Int): String {
        return String(
            requestBytes(
                "GET",
                url,
                ByteArray(0),
                "",
                emptyArray(),
                emptyArray(),
                timeoutMs,
                timeoutMs,
            ),
            Charsets.UTF_8,
        )
    }

    @JvmStatic
    fun requestBytes(
        method: String,
        url: String,
        body: ByteArray,
        contentType: String,
        headerNames: Array<String>,
        headerValues: Array<String>,
        connectTimeoutMs: Int,
        readTimeoutMs: Int,
    ): ByteArray {
        val normalizedMethod = method.uppercase()
        val requestBody = when {
            normalizedMethod == "GET" || normalizedMethod == "HEAD" -> null
            body.isNotEmpty() -> body.toRequestBody(contentType.toMediaTypeOrNull())
            contentType.isNotBlank() -> ByteArray(0).toRequestBody(contentType.toMediaTypeOrNull())
            else -> ByteArray(0).toRequestBody(null)
        }

        val builder = Request.Builder()
            .url(url)
            .method(normalizedMethod, requestBody)
        val headerCount = minOf(headerNames.size, headerValues.size)
        for (index in 0 until headerCount) {
            builder.addHeader(headerNames[index], headerValues[index])
        }
        val request = builder.build()

        try {
            return executeRequest(
                buildClient(connectTimeoutMs, readTimeoutMs, modernConnectionSpecs),
                request,
            )
        } catch (error: SSLHandshakeException) {
            if (!url.startsWith("https://", ignoreCase = true)) {
                throw error
            }
            return executeRequest(
                buildClient(connectTimeoutMs, readTimeoutMs, tls12OnlyConnectionSpecs),
                request,
            )
        }
    }
}
