---
--- Generated by EmmyLua(https://github.com/EmmyLua)
--- Created by guan.
--- DateTime: 2025/3/4 9:41
---

local MAIN_STATE=0

local TYPE_RUN=0
local TYPE_INVOKE=1
local TYPE_CALLBACK=2
local TYPE_SYNC_INVOKE=3

local M={}

local invoke_tool
local callback_tool
local syncinvoke_tool

invoke_tool=function(module,name,...)
    require(module)[name](...)
end

callback_tool=function(id,...)
    local callback=_thread.callback_get(id)
    callback(...)
    _thread.callback_gc(id)
end

--todo 同步调用可不可以不要这么蠢，是否可以提前获知表达式的左值和右值
syncinvoke_tool=function(module)  end

local table_preprocess
table_preprocess=function(t)
    --此处用pairs
    for k,v in pairs(t) do
        if type(v)=="function" then
            local a={id=_thread.callback_reg(v),
                     source=ID}
            setmetatable(a,{
                __call=_thread.callback_proxy
            })
            t[k]=a
        elseif type(v)=="table" then
            table_preprocess(v)
        end
    end
end

local parameter_preprocess=function(...)
    local temptable={...}
    --为确保传参顺序必须用ipairs，这是唯一与上述工具函数的区别
    for k,v in ipairs(temptable) do
        --如果有表格要递归进去找function
        print(k,v)
        if type(v)=="function" then
            local a={id=_thread.callback_reg(v),
                     source=ID}
            setmetatable(a,{
                __call=_thread.callback_proxy
            })
            temptable[k]=a
        --todo 测试lua fucntion能不能传
        elseif type(v)=="table" then
            table_preprocess(v)
        end
    end
    return table.unpack(temptable)
end

M.cross_vm_require= function(target_id,module)
    local proxy={}
    setmetatable(proxy,{
        __index = function(tb,key)
            return function(...)
                --为了保证初始调用线程被通知到，回调函数必须代理在初始线程调用
                local b,_sz=_seri.pack(module,key,parameter_preprocess(...))
                _thread.post(b,_sz,target_id,TYPE_INVOKE)
            end
        end
        })
    return proxy
end

--用于tick时读取任务
M.analyzer=function(buffer,sz,target_id,type)
    print(target_id,type)
    if target_id==ID then
        if type==TYPE_INVOKE then
            invoke_tool(_seri.unpack_remove(buffer))
        elseif type==TYPE_SYNC_INVOKE then
        --    todo 这里真正涉及到了调度，需要控制luaState的挂起和恢复

        elseif type==TYPE_CALLBACK then
            --FIXME 得益于luajava的引入，回调变得方便了起来
            --FIXME 但并不意味着这里可以省略，直接在lua层进行交互会快一点
            callback_tool(_seri.unpack_remove(buffer))
        elseif type==TYPE_RUN then
            local runnable=_seri.unpack_remove(buffer)
            runnable()
        end
    else
    --    todo 发错地方了，要重传
    end
end

return M
