package ai.gnus.sdk;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public class KeyStoreHelper {
    private static final String TAG = "KeyStoreHelper";
    private static final String ANDROID_KEYSTORE = "AndroidKeyStore";
    private static final String KEY_ALIAS = "SuperGeniusSecureStorage";
    private static final String TRANSFORMATION = "AES/GCM/NoPadding";
    private static final int GCM_TAG_LENGTH = 128;
    private static final String PREFS_NAME = "SuperGeniusSecurePrefs";
    private static final String PREFS_KEY = "encrypted_data";

    // Static singleton instance
    private static KeyStoreHelper sInstance = null;
    private static final Object sLock = new Object();
    
    private final Context context;

    // Private constructor for singleton
    private KeyStoreHelper(Context context) {
        this.context = context.getApplicationContext();
        initializeKeyStore();
    }
    
    /**
     * Initialize the KeyStoreHelper with application context.
     * Must be called once before using load/save/delete methods.
     * Typically called in Application.onCreate()
     */
    public static void initialize(Context context) {
        synchronized (sLock) {
            if (sInstance == null) {
                sInstance = new KeyStoreHelper(context);
                // Initialize native side with app ClassLoader
                nativeInit(context);
                Log.i(TAG, "KeyStoreHelper initialized");
            } else {
                Log.w(TAG, "KeyStoreHelper already initialized");
            }
        }
    }
    
    /**
     * Native initialization - called from Java to cache the class reference
     * using the app's ClassLoader instead of system ClassLoader
     */
    private static native void nativeInit(Context context);
    
    /**
     * Get the singleton instance.
     * Throws IllegalStateException if not initialized.
     */
    private static KeyStoreHelper getInstance() {
        synchronized (sLock) {
            if (sInstance == null) {
                throw new IllegalStateException(
                    "KeyStoreHelper not initialized. Call KeyStoreHelper.initialize(context) first."
                );
            }
            return sInstance;
        }
    }
    
    /**
     * Check if KeyStoreHelper has been initialized.
     */
    public static boolean isInitialized() {
        synchronized (sLock) {
            return sInstance != null;
        }
    }

    private void initializeKeyStore() {
        try {
            KeyStore keyStore = KeyStore.getInstance(ANDROID_KEYSTORE);
            keyStore.load(null);

            if (!keyStore.containsAlias(KEY_ALIAS)) {
                KeyGenerator keyGenerator = KeyGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_AES, 
                    ANDROID_KEYSTORE
                );

                KeyGenParameterSpec keyGenParameterSpec = new KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT
                )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setUserAuthenticationRequired(false)
                .build();

                keyGenerator.init(keyGenParameterSpec);
                keyGenerator.generateKey();
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize KeyStore", e);
        }
    }

    private SecretKey getSecretKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance(ANDROID_KEYSTORE);
        keyStore.load(null);
        return (SecretKey) keyStore.getKey(KEY_ALIAS, null);
    }

    // Static methods that delegate to singleton instance
    public static String load() {
        return getInstance().loadInternal();
    }
    
    public static boolean save(String data) {
        return getInstance().saveInternal(data);
    }
    
    public static boolean delete(String key) {
        return getInstance().deleteInternal(key);
    }
    
    // Internal implementation methods
    private String loadInternal() {
        try {
            SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            String encryptedData = prefs.getString(PREFS_KEY, null);

            if (encryptedData == null) {
                return null;
            }

            return decrypt(encryptedData);
        } catch (Exception e) {
            Log.e(TAG, "Failed to load data", e);
            return null;
        }
    }

    private boolean saveInternal(String data) {
        try {
            String encryptedData = encrypt(data);
            
            SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            return prefs.edit().putString(PREFS_KEY, encryptedData).commit();
        } catch (Exception e) {
            Log.e(TAG, "Failed to save data", e);
            return false;
        }
    }

    private boolean deleteInternal(String key) {
        try {
            SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            return prefs.edit().remove(PREFS_KEY).commit();
        } catch (Exception e) {
            Log.e(TAG, "Failed to delete data", e);
            return false;
        }
    }

    private String encrypt(String data) throws Exception {
        SecretKey secretKey = getSecretKey();
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        cipher.init(Cipher.ENCRYPT_MODE, secretKey);

        byte[] iv = cipher.getIV();
        byte[] encrypted = cipher.doFinal(data.getBytes(StandardCharsets.UTF_8));

        byte[] combined = new byte[iv.length + encrypted.length];
        System.arraycopy(iv, 0, combined, 0, iv.length);
        System.arraycopy(encrypted, 0, combined, iv.length, encrypted.length);

        return Base64.encodeToString(combined, Base64.DEFAULT);
    }

    private String decrypt(String encryptedData) throws Exception {
        byte[] combined = Base64.decode(encryptedData, Base64.DEFAULT);

        byte[] iv = new byte[12];
        byte[] encrypted = new byte[combined.length - iv.length];
        System.arraycopy(combined, 0, iv, 0, iv.length);
        System.arraycopy(combined, iv.length, encrypted, 0, encrypted.length);

        SecretKey secretKey = getSecretKey();
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        cipher.init(Cipher.DECRYPT_MODE, secretKey, new GCMParameterSpec(GCM_TAG_LENGTH, iv));

        byte[] decrypted = cipher.doFinal(encrypted);
        return new String(decrypted, StandardCharsets.UTF_8);
    }
}
