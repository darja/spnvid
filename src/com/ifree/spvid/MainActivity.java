package com.ifree.spvid;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;

public class MainActivity extends Activity {
    private static final String TAG = "SpoonyVid";

    static {
        System.loadLibrary("vid");
    }

    private static native int encode(int framesCount);
    private static native void helloLog(String entry);

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
    }

    public void onGenerate(View view) {
        helloLog("Here I am");
        int result = encode(86);
        Log.d(TAG, "Encoding result = " + result);
    }
}