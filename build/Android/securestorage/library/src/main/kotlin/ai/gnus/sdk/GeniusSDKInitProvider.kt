package ai.gnus.sdk

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.net.Uri
import android.util.Log

class GeniusSDKInitProvider : ContentProvider() {

    companion object {
        private const val TAG = "GeniusSDKInit"
    }

    override fun onCreate(): Boolean {
        Log.i(TAG, "GeniusSDKInitProvider onCreate — auto-initializing background processing")

        val context = context ?: run {
            Log.e(TAG, "GeniusSDKInitProvider: context is null — cannot initialize")
            return false
        }

        try {
            BackgroundServiceManager.initialize(context.applicationContext)
            Log.i(TAG, "BackgroundServiceManager auto-initialized via ContentProvider")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to auto-initialize BackgroundServiceManager", e)
        }

        return true
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor? = null

    override fun getType(uri: Uri): String? = null

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?
    ): Int = 0
}
