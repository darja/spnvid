LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_LDLIBS += -llog
LOCAL_STATIC_LIBRARIES := libavformat libavcodec libpostproc libswscale libavutil libavcore 
LOCAL_C_INCLUDES += $(LOCAL_PATH)/ffmpeg
LOCAL_SRC_FILES := vid.c
LOCAL_MODULE    := vid

include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH := $(call my-dir)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/ffmpeg
include $(all-subdir-makefiles)
