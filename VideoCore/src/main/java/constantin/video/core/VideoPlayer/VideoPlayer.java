package constantin.video.core.VideoPlayer;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Environment;
import android.util.Log;
import android.view.Surface;

import java.io.File;
import java.util.Timer;
import java.util.TimerTask;

import constantin.video.core.DecodingInfo;
import constantin.video.core.IVideoParamsChanged;


//Convenient wrapper around the native functions from VideoNative
public class VideoPlayer implements INativeVideoParamsChanged {
    private static final String TAG="VideoPlayer";
    public static final int VS_SOURCE_UDP=0;
    public static final int VS_SOURCE_FILE=1;
    public static final int VS_SOURCE_ASSETS =2;
    public static final int VS_SOURCE_FFMPEG_URL=3;
    public static final int VS_SOURCE_EXTERNAL=4;
    public enum VS_SOURCE{UDP,FILE,ASSETS,FFMPEG,EXTERNAL}
    //Members
    private final long nativeVideoPlayer;
    private IVideoParamsChanged mVideoParamsChanged;
    private final Context context;
    private Timer timer;

    //Setup as much as possible without creating the decoder
    //It is not recommended to change Settings in the Shared Preferences after instantiating the Video Player
    public VideoPlayer(final Context context,final IVideoParamsChanged iVideoParamsChanged){
        this.mVideoParamsChanged =iVideoParamsChanged;
        this.context=context;
        nativeVideoPlayer= nativeInitialize(context,VideoSettings.getDirectoryToSaveDataTo());
    }

    public void setIVideoParamsChanged(final IVideoParamsChanged iVideoParamsChanged){
        mVideoParamsChanged=iVideoParamsChanged;
    }

    //Depending on the selected Settings, this starts either
    //a) Receiving RAW over UDP
    //b) Receiving RTP over UDP
    //c) Receiving Data from a resource file (Assets)
    //d) Receiving Data from a file in the phone file system
    public void addAndStartDecoderReceiver(Surface surface){
        nativeStart(nativeVideoPlayer,surface,context.getAssets());
        if(mVideoParamsChanged !=null){
            final INativeVideoParamsChanged interfaceVideoParamsChanged=this;
            Log.d(TAG,"Starting timer");
            //The timer initiates the callback(s), but if no data has changed they are not called (and the timer does almost no work)
            //TODO: proper queue, but how to do synchronization in java ndk ?!
            timer=new Timer();
            timer.schedule(new TimerTask() {
                @Override
                public void run() {
                    nativeCallBack(interfaceVideoParamsChanged,nativeVideoPlayer);
                }
            },0,500);
        }
    }

    //Stop the Receiver
    //Stop the Decoder
    //Free resources
    public void stopAndRemoveReceiverDecoder(){
        if(mVideoParamsChanged !=null){
            timer.cancel();
            timer.purge();
            Log.d(TAG,"Stopped timer");
        }
        nativeStop(nativeVideoPlayer);
    }

    public long getNativeInstance(){
        return nativeVideoPlayer;
    }

    /**
     * called by native code via NDK
     */
    @Override
    @SuppressWarnings({"UnusedDeclaration"})
    public void onVideoRatioChanged(int videoW, int videoH) {
        if(mVideoParamsChanged !=null){
            mVideoParamsChanged.onVideoRatioChanged(videoW,videoH);
        }
        //System.out.println("Video W and H"+videoW+","+videoH);
    }

    /**
     * called by native code via NDK
     */
    @Override
    @SuppressWarnings({"UnusedDeclaration"})
    public void onDecodingInfoChanged(float currentFPS, float currentKiloBitsPerSecond, float avgParsingTime_ms, float avgWaitForInputBTime_ms, float avgDecodingTime_ms,
                                      int nNALU,int nNALUSFeeded) {
        final DecodingInfo decodingInfo=new DecodingInfo(currentFPS,currentKiloBitsPerSecond,avgParsingTime_ms,avgWaitForInputBTime_ms,avgDecodingTime_ms,nNALU,nNALUSFeeded);
        if(mVideoParamsChanged !=null){
            mVideoParamsChanged.onDecodingInfoChanged(decodingInfo);
        }
        Log.d(TAG,"onDecodingInfoChanged"+decodingInfo.toString());
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            nativeFinalize(nativeVideoPlayer);
        } finally {
            super.finalize();
        }
    }

    //All the native binding(s)
    static {
        System.loadLibrary("VideoNative");
    }
    public static native long nativeInitialize(Context context, String groundRecordingDirectory);
    public static native void nativeFinalize(long nativeVideoPlayer);

    public static native void nativePassNALUData(long nativeInstance,byte[] b,int offset,int size);

    public static native void nativeStart(long nativeInstance, Surface surface, AssetManager assetManager);
    public static native void nativeStop(long nativeInstance);

    /**
     * Debugging/ Testing only
     */
    public static native String getVideoInfoString(long nativeInstance);
    public static native boolean anyVideoDataReceived(long nativeInstance);
    public static native boolean anyVideoBytesParsedSinceLastCall(long nativeInstance);
    public static native boolean receivingVideoButCannotParse(long nativeInstance);

    //call this via java to run the callback(s)
    //TODO: Use message queue from cpp for performance
    public static native <T extends INativeVideoParamsChanged> void nativeCallBack(T t, long nativeInstance);
    //End native binding(s)

}