//
// Created by guan on 2025/3/13.
//
#include <unistd.h>
#include "Scheduler.h"
#include "lauxlib.h"
#include "lua-seri.h"
#include "Thread.h"

static const char* THREAD_BUNDLE_KEY = "__THREAD_BUNDLE";

static threadBundle * bundleGet(lua_State* L){
    lua_pushstring(L, THREAD_BUNDLE_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);  // 从注册表获取值

    threadBundle* tbun = (threadBundle*)lua_touserdata(L, -1);
    lua_pop(L, 1);  // 弹出值
    return tbun;
}

static void bundleSet(lua_State* L, threadBundle* tbun){
    lua_pushstring(L, THREAD_BUNDLE_KEY);
    lua_pushlightuserdata(L, tbun);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

int luasched_fork(lua_State* L){
    //fork(id)
    lua_getglobal(L,"ID");
    int selfid= lua_tointeger(L,2);

    threadBundle * tbun= bundleGet(L);
    if(!tbun) {
        //初始化
        tbun = (threadBundle *) malloc(sizeof(threadBundle));
        tbun->parent = selfid;
        tbun->threads=map_init(numcmp);
        atomic_init(&tbun->alive, 0);
        pthread_cond_init(&tbun->cond,NULL);
        pthread_mutex_init(&tbun->mutex,NULL);
        tbun->bundle_size=0;

        bundleSet(L, tbun);
    }
    //新建图节点
    int* key=(int*) malloc(sizeof(int));
    *key= lua_tointeger(L,1);
    ret_record* val=(ret_record*) malloc(sizeof(ret_record));
    atomic_init(&val->flag,1);
    val->buf=NULL;
    //todo 注意非线程安全
    map_insert(tbun->threads, key, (void*)val);
    tbun->bundle_size++;

    //加alive
    atomic_fetch_add(&tbun->alive,1);

    //回传meta
    lua_pushlightuserdata(L,tbun);
    return 1;
}

int luasched_ret(lua_State* L){
//    _sched.ret(meta,buf)
    threadBundle* meta= (threadBundle*)lua_touserdata(L,1);
    void* buf=lua_touserdata(L,2);
    lua_getglobal(L,"ID");
    int id=lua_tointeger(L,3);

    mapNode* node= map_search(meta->threads,&id);
    ((ret_record *)node->value)->buf=buf;
    atomic_fetch_and(&((ret_record*)node->value)->flag,0);

    atomic_fetch_sub(&meta->alive,1);
    if(!atomic_load(&meta->alive)){
        pthread_mutex_lock(&meta->mutex);
        pthread_cond_signal(&meta->cond);
        pthread_mutex_unlock(&meta->mutex);
    }
    return 0;
}

int luasched_join(lua_State* L){
    //join(id)

    int join_id= lua_tointeger(L,1);
    lua_getglobal(L,"ID");
    int selfid=lua_tointeger(L,-1);

    threadBundle * tbun= bundleGet(L);
    if (!tbun) {
        luaL_error(L, "threadBundle not initialized");
        return 0;
    }
    ret_record * rtr=map_find(tbun->threads,&join_id);

    //查flag，看线程是否结束，是一个自旋锁
    while(atomic_load(&rtr->flag)){
        usleep(100);
    }

    //清理线程树的节点
    map_erase(tbun->threads,&join_id);
    void* buffer=rtr->buf;
    free(rtr);
    tbun->bundle_size--;

    //回收线程
    free_thread(join_id);

    //释放哈希表
    if(!tbun->bundle_size) {
        // 销毁锁
        pthread_cond_destroy(&tbun->cond);
        pthread_mutex_destroy(&tbun->mutex);
        // 从注册表移除 threadBundle
        lua_pushstring(L, THREAD_BUNDLE_KEY);
        lua_pushnil(L);
        lua_rawset(L, LUA_REGISTRYINDEX);
        // 释放 threadBundle 内存
        free(tbun);
    }

    if(buffer)
        return  seri_unpack(L, buffer);
    else
        return 0;
}

//若要调用wait，先wait后join
int luasched_wait(lua_State* L){
    lua_getglobal(L,"ID");
    threadBundle * tbun= bundleGet(L);

    if (!tbun) {
        luaL_error(L, "threadBundle not initialized");
        return 0;
    }

    pthread_mutex_lock(&tbun->mutex);
    while (!atomic_load(&tbun->alive)) {
        pthread_cond_wait(&tbun->cond,&tbun->mutex);
    }
    pthread_mutex_unlock(&tbun->mutex);

    return 0;
}

LUAMOD_API int
luaopen_sched(lua_State *L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
            {"fork",luasched_fork},
            {"ret",luasched_ret},
            {"join",luasched_join},
            {"wait",luasched_wait},
            { NULL, NULL },
    };
    luaL_newlib(L, l);
    return 1;
}
