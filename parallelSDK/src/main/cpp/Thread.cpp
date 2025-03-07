//
// Created by guan on 2025/3/4.
//

#include <jni.h>
#include "Thread.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "lua-seri.h"
}
#include "JNInfo.h"
#include "TaskQueue.h"


static int new_thread(){
    JNIEnv *env;
    jvm->AttachCurrentThread(&env, NULL);
    jclass clazz = env->FindClass("com/lockheed/parallelsdk/parallelSDK");
    jmethodID tick = env->GetStaticMethodID( clazz,"Tick","(I)V");
    jmethodID getInstance = env->GetStaticMethodID( clazz,"getInstance", "()Lcom/lockheed/parallelsdk/parallelSDK;");
    jobject sdkInstance = env->CallStaticObjectMethod(  clazz, getInstance);
    jmethodID mtd = env->GetMethodID(  clazz, "luaNewStateC", "()I");
    jint id=env->CallIntMethod(  sdkInstance, mtd);
//    jvm->DetachCurrentThread();
    return id;
}

int luathread_new(lua_State* L){
    lua_pushinteger(L,new_thread());
    return 1;
}

int luathread_free(lua_State* L){
    lua_getglobal(L,"ID");
    int id= lua_tointeger(L,-1);
    CirQueClose(id);
    JNIEnv *env;
    jvm->AttachCurrentThread(&env, NULL);
    jclass clazz = env->FindClass("com/lockheed/parallelsdk/parallelSDK");
    jmethodID tick = env->GetStaticMethodID( clazz,"Tick","(I)V");
    jmethodID getInstance = env->GetStaticMethodID( clazz,"getInstance", "()Lcom/lockheed/parallelsdk/parallelSDK;");
    jobject sdkInstance = env->CallStaticObjectMethod(  clazz, getInstance);
    jmethodID mtd = env->GetMethodID(  clazz, "closeLuaStateC", "(I)V");
    env->CallVoidMethod(sdkInstance, mtd,id);
    jvm->DetachCurrentThread();
    return 0;
}

int luathread_post(lua_State* L){
    //todo:应该要通知对方才行，因为不能一直tick去占用cpu，通知上面handler去发一个popstack
    luatask_push(L);
    JNIEnv *env;
    jvm->AttachCurrentThread(&env, NULL);
    jclass clazz = env->FindClass("com/lockheed/parallelsdk/parallelSDK");
    jmethodID tick = env->GetStaticMethodID( clazz,"Tick","(I)V");
    env->CallStaticVoidMethod(clazz,tick,(int)lua_tointeger(L,3));
//    jvm->DetachCurrentThread();
    return 0;
}

//#define batch 20
int luathread_processTask(lua_State* L){
//    for(int i=0;i<batch;i++){
    int ret=luatask_pop(L);
//    }
    if(queue_record.at((int)lua_tointeger(L,3))->size!=0){
        JNIEnv *env;
        jvm->AttachCurrentThread(&env, NULL);
        jclass clazz = env->FindClass("com/lockheed/parallelsdk/parallelSDK");
        jmethodID tick = env->GetStaticMethodID( clazz,"Tick","(I)V");
        env->CallStaticVoidMethod(clazz,tick,(int)lua_tointeger(L,3));
//        jvm->DetachCurrentThread();
    }
    return ret;
}

int luathread_callbackreg(lua_State* L){
    //传一个参function
    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // 存储到 Registry
    lua_pushinteger(L, ref);
    return 1;
}

int luathread_callbackget(lua_State* L){
    //传一个参id
    int id= lua_tointeger(L,1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, id);
    return 1;
}

int luathread_callproxy(lua_State* L);

static void process_table(lua_State *L, int index) {
    /* 第一个键 */
    lua_pushnil(L);

    while (lua_next(L, -2) != 0) {
        /* 键在-2，值在-1 */
        int value_type = lua_type(L, -1);

        if (value_type == LUA_TFUNCTION) {
            lua_newtable(L);        //+1

            lua_pushvalue(L,-1);  //+1
            luathread_callbackreg(L);   //+1,-1

            lua_setfield(L, -2, "id");
            lua_getglobal(L, "ID");
            lua_setfield(L, -2, "source");

            lua_newtable(L);
            lua_pushcfunction(L, luathread_callproxy);
            lua_setfield(L, -2, "__call");
            lua_setmetatable(L, -2);

            //原键在-2，新表在-1，table在index
            //重新赋值
            lua_settable(L, index);
        } else if (value_type == LUA_TTABLE) {
            process_table(L, lua_gettop(L));
        } else {
            lua_pop(L, 1);  // 弹出值,留下键给next函数
        }
    }
}

static int l_parameter_preprocess(lua_State *L) {
    int n = lua_gettop(L);
    int i;
    for(i=1;i<=n;i++){
        int value_type = lua_type(L, i);
        if (value_type == LUA_TFUNCTION) {
            lua_newtable(L);        //+1

            lua_pushvalue(L,i);  //+1
            luathread_callbackreg(L);   //+1,-1

            lua_setfield(L, -2, "id");
            lua_getglobal(L, "ID");
            lua_setfield(L, -2, "source");

            lua_newtable(L);
            lua_pushcfunction(L, luathread_callproxy);
            lua_setfield(L, -2, "__call");
            lua_setmetatable(L, -2);

            /* 替换数组元素 */
            lua_replace(L,i);
        } else if (value_type == LUA_TTABLE) {
            process_table(L, i);
        }
    }

    return n;  // 返回参数个数
}


int luathread_callproxy(lua_State* L){
    /*
    local a={id=_thread.callback_reg(v),
            source=ID}
        setmetatable(a,{
            __call=function(self,...)
                local b,_sz=seri.pack(self.id,...)
                _thread.post(b,_sz,self.source,TYPE_CALLBACK)
            end
        })
    */
    lua_getfield(L, 1, "id");
    // id留在栈顶
    lua_getfield(L, 1, "source");
    int source = luaL_checkinteger(L, -1);
    lua_pop(L, 1); // 平栈
    //现在栈顶是id
    lua_replace(L,1);     //栈底的self换成self.id

    l_parameter_preprocess(L);
    luaseri_pack(L);

    // 调用 _thread.post(b, _sz, source, TYPE_CALLBACK)
    //现在最顶上有2个返回值
    lua_getglobal(L, "_thread");
    lua_getfield(L, -1, "post");
    lua_pushvalue(L, -4); // b
    lua_pushvalue(L, -4); // _sz
    lua_pushinteger(L, source);
    lua_pushinteger(L,TYPE_CALLBACK);
    lua_call(L, 4, 0); // 调用 _thread.post

    return 0;
}

int luathread_callbackgc(lua_State* L){
    int id= lua_tointeger(L,1);
    luaL_unref(L, LUA_REGISTRYINDEX, id);
    return 0;
}

LUAMOD_API int
luaopen_thread(lua_State *L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
            {"new",luathread_new},
            {"post",luathread_post},
            {"free",luathread_free},
            {"processtask",luathread_processTask},
            {"callback_reg", luathread_callbackreg},
            {"callback_get",luathread_callbackget},
            {"callback_proxy",luathread_callproxy},
            {"callback_gc",luathread_callbackgc},
            { NULL, NULL },
    };
    luaL_newlib(L, l);
    return 1;
}