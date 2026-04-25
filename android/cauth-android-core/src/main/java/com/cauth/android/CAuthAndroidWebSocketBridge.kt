package com.cauth.android

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

object CAuthAndroidWebSocketBridge {
    private const val CancelPollIntervalMs = 50L

    private val nextHandle = AtomicLong(1L)
    private val sessions = ConcurrentHashMap<Long, WebSocketSession>()

    @JvmStatic
    fun connect(url: String, connectTimeoutMs: Int): Long {
        val client = OkHttpClient.Builder()
            .connectTimeout(connectTimeoutMs.toLong(), TimeUnit.MILLISECONDS)
            .readTimeout(0, TimeUnit.MILLISECONDS)
            .build()

        val opened = CountDownLatch(1)
        val session = WebSocketSession(client)
        val request = Request.Builder().url(url).build()
        val webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                session.webSocket = webSocket
                opened.countDown()
            }

            override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                session.messages.offer(bytes.toByteArray())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                session.messages.offer(text.toByteArray(Charsets.UTF_8))
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                session.failure = t
                opened.countDown()
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                session.closed = true
                opened.countDown()
            }
        })

        val deadlineNanos = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(connectTimeoutMs.toLong())
        while (session.webSocket == null && session.failure == null && !session.closed) {
            if (CAuthNativeCore.nativeIsOperationCanceled()) {
                webSocket.cancel()
                client.dispatcher.executorService.shutdown()
                throw IOException("operation canceled")
            }
            if (opened.await(CancelPollIntervalMs, TimeUnit.MILLISECONDS)) {
                break
            }
            if (System.nanoTime() >= deadlineNanos) {
                webSocket.cancel()
                client.dispatcher.executorService.shutdown()
                throw IOException("websocket connect timed out")
            }
        }

        session.failure?.let {
            webSocket.cancel()
            client.dispatcher.executorService.shutdown()
            throw IOException("websocket connect failed", it)
        }

        if (session.webSocket == null) {
            webSocket.cancel()
            client.dispatcher.executorService.shutdown()
            throw IOException("websocket connect failed")
        }

        val handle = nextHandle.getAndIncrement()
        sessions[handle] = session
        return handle
    }

    @JvmStatic
    fun sendBinary(handle: Long, bytes: ByteArray) {
        val session = sessions[handle] ?: throw IOException("invalid websocket handle")
        val webSocket = session.webSocket ?: throw IOException("websocket is not open")
        if (CAuthNativeCore.nativeIsOperationCanceled()) {
            throw IOException("operation canceled")
        }
        if (!webSocket.send(ByteString.of(*bytes))) {
            throw IOException("websocket send failed")
        }
    }

    @JvmStatic
    fun receiveBinary(handle: Long, receiveTimeoutMs: Int): ByteArray {
        val session = sessions[handle] ?: throw IOException("invalid websocket handle")
        val deadlineNanos = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(receiveTimeoutMs.toLong())
        while (true) {
            session.failure?.let { throw IOException("websocket receive failed", it) }
            if (CAuthNativeCore.nativeIsOperationCanceled()) {
                session.webSocket?.cancel()
                session.client.dispatcher.executorService.shutdown()
                session.closed = true
                throw IOException("operation canceled")
            }
            val bytes = session.messages.poll(CancelPollIntervalMs, TimeUnit.MILLISECONDS)
            if (bytes != null) {
                return bytes
            }
            if (session.closed) {
                throw IOException("websocket is closing")
            }
            if (System.nanoTime() >= deadlineNanos) {
                throw IOException("websocket receive timed out")
            }
        }
    }

    @JvmStatic
    fun close(handle: Long) {
        val session = sessions.remove(handle) ?: return
        session.webSocket?.close(1000, "closed")
        session.client.dispatcher.executorService.shutdown()
    }

    private class WebSocketSession(
        val client: OkHttpClient,
    ) {
        @Volatile
        var webSocket: WebSocket? = null

        @Volatile
        var failure: Throwable? = null

        @Volatile
        var closed: Boolean = false

        val messages = LinkedBlockingQueue<ByteArray>()
    }
}
