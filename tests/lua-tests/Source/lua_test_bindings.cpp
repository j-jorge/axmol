/****************************************************************************
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

 https://axmol.dev/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "lua_test_bindings.h"
#include "cocos2d.h"
#include "lua-bindings/manual/LuaBasicConversions.h"

NS_AX_BEGIN

/**
 * Copy DrawNode for 3D geometry drawing.
 */
class DrawNode3D : public Node
{
public:
    /** creates and initialize a DrawNode3D node */
    static DrawNode3D* create();

    /**
     * Draw 3D Line
     */
    void drawLine(const Vec3& from, const Vec3& to, const Color4F& color);

    /**
     * Draw 3D cube
     * @param point to a vertex array who has 8 element.
     *        vertices[0]:Left-top-front,
     *        vertices[1]:Left-bottom-front,
     *        vertices[2]:Right-bottom-front,
     *        vertices[3]:Right-top-front,
     *        vertices[4]:Right-top-back,
     *        vertices[5]:Right-bottom-back,
     *        vertices[6]:Left-bottom-back,
     *        vertices[7]:Left-top-back.
     * @param color
     */
    void drawCube(Vec3* vertices, const Color4F& color);

    /** Clear the geometry in the node's buffer. */
    void clear();

    /**
     * @js NA
     * @lua NA
     */
    const BlendFunc& getBlendFunc() const;

    /**
     * @code
     * When this function bound into js or lua,the parameter will be changed
     * In js: var setBlendFunc(var src, var dst)
     * @endcode
     * @lua NA
     */
    void setBlendFunc(const BlendFunc& blendFunc);

    // Overrides
    virtual void draw(Renderer* renderer, const Mat4& transform, uint32_t flags) override;

    DrawNode3D();
    virtual ~DrawNode3D();
    virtual bool init() override;

protected:
    struct V3F_C4B
    {
        Vec3 vertices;
        Color4B colors;
    };
    void ensureCapacity(int count);

    std::vector<V3F_C4B> _buffer;

    BlendFunc _blendFunc = BlendFunc::ALPHA_PREMULTIPLIED;
    CustomCommand _customCommand;
    backend::ProgramState* _programState = nullptr;
    bool _dirty                          = false;
    backend::UniformLocation _locMVPMatrix;

private:
    AX_DISALLOW_COPY_AND_ASSIGN(DrawNode3D);

    bool _rendererDepthTestEnabled = false;

    void onBeforeDraw();
    void onAfterDraw();
};

DrawNode3D::DrawNode3D() {}

DrawNode3D::~DrawNode3D()
{
    AX_SAFE_RELEASE_NULL(_programState);
}

DrawNode3D* DrawNode3D::create()
{
    DrawNode3D* ret = new DrawNode3D();
    if (ret->init())
    {
        ret->autorelease();
    }
    else
    {
        AX_SAFE_DELETE(ret);
    }

    return ret;
}

void DrawNode3D::ensureCapacity(int count)
{
    AXASSERT(count >= 0, "capacity must be >= 0");

    if (_buffer.size() + count > _buffer.capacity())
    {
        auto newSize = MAX(_buffer.capacity() + (_buffer.capacity() >> 1), count + _buffer.size());
        _buffer.reserve(newSize);
    }
}

bool DrawNode3D::init()
{

    _blendFunc    = BlendFunc::ALPHA_PREMULTIPLIED;
    auto program  = backend::Program::getBuiltinProgram(backend::ProgramType::LINE_COLOR_3D);
    _programState = new backend::ProgramState(program);

    _locMVPMatrix = _programState->getUniformLocation("u_MVPMatrix");

#define INITIAL_VERTEX_BUFFER_LENGTH 512
    ensureCapacity(INITIAL_VERTEX_BUFFER_LENGTH);

    _customCommand.setDrawType(CustomCommand::DrawType::ARRAY);
    _customCommand.setPrimitiveType(CustomCommand::PrimitiveType::LINE);

    const auto& attributeInfo = _programState->getProgram()->getActiveAttributes();
    auto iter                 = attributeInfo.find("a_position");
    auto vertexLayout         = _programState->getMutableVertexLayout();
    if (iter != attributeInfo.end())
    {
        vertexLayout->setAttrib(iter->first, iter->second.location, backend::VertexFormat::FLOAT3, 0, false);
    }
    iter = attributeInfo.find("a_color");
    if (iter != attributeInfo.end())
    {
        vertexLayout->setAttrib(iter->first, iter->second.location, backend::VertexFormat::UBYTE4, sizeof(Vec3),
                                      true);
    }
    vertexLayout->setStride(sizeof(V3F_C4B));

    _customCommand.createVertexBuffer(sizeof(V3F_C4B), INITIAL_VERTEX_BUFFER_LENGTH,
                                      CustomCommand::BufferUsage::DYNAMIC);

    _customCommand.getPipelineDescriptor().programState = _programState;

    _dirty = true;

    _customCommand.setBeforeCallback(AX_CALLBACK_0(DrawNode3D::onBeforeDraw, this));
    _customCommand.setAfterCallback(AX_CALLBACK_0(DrawNode3D::onAfterDraw, this));

#if AX_ENABLE_CACHE_TEXTURE_DATA
    // Need to listen the event only when not use batchnode, because it will use VBO
    auto listener = EventListenerCustom::create(EVENT_COME_TO_FOREGROUND, [this](EventCustom* event) {
        /** listen the event that coming to foreground on Android */
        this->init();
    });

    _eventDispatcher->addEventListenerWithSceneGraphPriority(listener, this);
#endif

    return true;
}

void DrawNode3D::draw(Renderer* renderer, const Mat4& transform, uint32_t flags)
{
    _customCommand.init(_globalZOrder);
    // update mvp matrix
    auto& matrixP = Director::getInstance()->getMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);
    auto mvp      = matrixP * transform;
    _programState->setUniform(_locMVPMatrix, mvp.m, sizeof(mvp.m));

    if (_customCommand.getVertexCapacity() < _buffer.size())
    {
        auto s = _buffer.size();
        _customCommand.createVertexBuffer(sizeof(V3F_C4B), s + s / 2, CustomCommand::BufferUsage::DYNAMIC);
    }

    if (_dirty && !_buffer.empty())
    {
        _customCommand.updateVertexBuffer(_buffer.data(), (unsigned int)(_buffer.size() * sizeof(_buffer[0])));
        _customCommand.setVertexDrawInfo(0, _buffer.size());
        _dirty = false;
    }

    setBlendFunc(_blendFunc);

    if (!_buffer.empty())
    {
        renderer->addCommand(&_customCommand);
    }
}

void DrawNode3D::drawLine(const Vec3& from, const Vec3& to, const Color4F& color)
{
    unsigned int vertex_count = 2;
    ensureCapacity(vertex_count);

    Color4B col = Color4B(color);
    V3F_C4B a   = {Vec3(from.x, from.y, from.z), col};
    V3F_C4B b   = {
        Vec3(to.x, to.y, to.z),
        col,
    };
    _buffer.emplace_back(a);
    _buffer.emplace_back(b);
    _dirty = true;
}

void DrawNode3D::drawCube(Vec3* vertices, const Color4F& color)
{
    // front face
    drawLine(vertices[0], vertices[1], color);
    drawLine(vertices[1], vertices[2], color);
    drawLine(vertices[2], vertices[3], color);
    drawLine(vertices[3], vertices[0], color);

    // back face
    drawLine(vertices[4], vertices[5], color);
    drawLine(vertices[5], vertices[6], color);
    drawLine(vertices[6], vertices[7], color);
    drawLine(vertices[7], vertices[4], color);

    // edge
    drawLine(vertices[0], vertices[7], color);
    drawLine(vertices[1], vertices[6], color);
    drawLine(vertices[2], vertices[5], color);
    drawLine(vertices[3], vertices[4], color);
}

void DrawNode3D::clear()
{
    _dirty = true;
    _buffer.clear();
}

const BlendFunc& DrawNode3D::getBlendFunc() const
{
    return _blendFunc;
}

void DrawNode3D::setBlendFunc(const BlendFunc& blendFunc)
{
    _blendFunc = blendFunc;
    // update blend mode
    auto& blend                = _customCommand.getPipelineDescriptor().blendDescriptor;
    blend.blendEnabled         = true;
    blend.sourceRGBBlendFactor = blend.sourceAlphaBlendFactor = _blendFunc.src;
    blend.destinationRGBBlendFactor = blend.destinationAlphaBlendFactor = _blendFunc.dst;
}

void DrawNode3D::onBeforeDraw()
{
    auto* renderer            = Director::getInstance()->getRenderer();
    _rendererDepthTestEnabled = renderer->getDepthTest();
    renderer->setDepthTest(true);
}

void DrawNode3D::onAfterDraw()
{
    auto* renderer = Director::getInstance()->getRenderer();
    renderer->setDepthTest(_rendererDepthTestEnabled);
}

/**
 @since v3.3rc1
 This class is used to check if the the value type judgement in table is correct or not.
 eg:
 If call `create` by passing {index1 = 111, index2 = 112, index3 = 113} from lua,
 the type 111,112,113 would be judged as string type before 3.3rc1
 **/
class ValueTypeJudgeInTable : public Node
{
public:
    static ValueTypeJudgeInTable* create(ValueMap valueMap);
};

ValueTypeJudgeInTable* ValueTypeJudgeInTable::create(ValueMap valueMap)
{
    ValueTypeJudgeInTable* ret = new ValueTypeJudgeInTable();
    ret->autorelease();

    int index = 0;
    for (const auto& iter : valueMap)
    {
        Value::Type type = iter.second.getTypeFamily();
        if (type == Value::Type::STRING)
        {
            AXLOGD("The type of index {} is string", index);
        }

        if (type == Value::Type::INTEGER || type == Value::Type::DOUBLE || type == Value::Type::FLOAT)
        {
            AXLOGD("The type of index {} is number", index);
        }

        ++index;
    }

    return ret;
}
NS_AX_END

int lua_cocos2dx_DrawNode3D_getBlendFunc(lua_State* L)
{
    int argc                  = 0;
    ax::DrawNode3D* cobj = nullptr;
    bool ok                   = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertype(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    cobj = (ax::DrawNode3D*)tolua_tousertype(L, 1, 0);

#if _AX_DEBUG >= 1
    if (!cobj)
    {
        tolua_error(L, "invalid 'cobj' in function 'lua_cocos2dx_DrawNode3D_getBlendFunc'", nullptr);
        return 0;
    }
#endif

    argc = lua_gettop(L) - 1;
    if (argc == 0)
    {
        if (!ok)
            return 0;
        const ax::BlendFunc& ret = cobj->getBlendFunc();
        blendfunc_to_luaval(L, ret);
        return 1;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {} \n", "ax.DrawNode3D:getBlendFunc", argc, 0);
    return 0;

#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_getBlendFunc'.", &tolua_err);
#endif

    return 0;
}

int lua_cocos2dx_DrawNode3D_setBlendFunc(lua_State* L)
{
    int argc                  = 0;
    ax::DrawNode3D* cobj = nullptr;
    bool ok                   = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertype(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    cobj = (ax::DrawNode3D*)tolua_tousertype(L, 1, 0);

#if _AX_DEBUG >= 1
    if (!cobj)
    {
        tolua_error(L, "invalid 'cobj' in function 'lua_cocos2dx_DrawNode3D_setBlendFunc'", nullptr);
        return 0;
    }
#endif

    argc = lua_gettop(L) - 1;
    if (argc == 1)
    {
        ax::BlendFunc arg0;

        ok &= luaval_to_blendfunc(L, 2, &arg0, "ax.Sprite3D:setBlendFunc");
        if (!ok)
        {
            tolua_error(L, "invalid arguments in function 'lua_cocos2dx_DrawNode3D_setBlendFunc'", nullptr);
            return 0;
        }
        cobj->setBlendFunc(arg0);
        return 0;
    }

    AXLOGD("{} has wrong number of arguments: {}, was expecting {} \n", "ax.DrawNode3D:setBlendFunc", argc, 1);
    return 0;

#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_setBlendFunc'.", &tolua_err);
#endif

    return 0;
}

int lua_cocos2dx_DrawNode3D_drawLine(lua_State* L)
{
    int argc                  = 0;
    ax::DrawNode3D* cobj = nullptr;
    bool ok                   = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertype(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    cobj = (ax::DrawNode3D*)tolua_tousertype(L, 1, 0);

#if _AX_DEBUG >= 1
    if (!cobj)
    {
        tolua_error(L, "invalid 'cobj' in function 'lua_cocos2dx_DrawNode3D_drawLine'", nullptr);
        return 0;
    }
#endif

    argc = lua_gettop(L) - 1;
    if (argc == 3)
    {
        ax::Vec3 arg0;
        ax::Vec3 arg1;
        ax::Color4F arg2;

        ok &= luaval_to_vec3(L, 2, &arg0, "ax.DrawNode3D:drawLine");

        ok &= luaval_to_vec3(L, 3, &arg1, "ax.DrawNode3D:drawLine");

        ok &= luaval_to_color4f(L, 4, &arg2, "ax.DrawNode3D:drawLine");
        if (!ok)
            return 0;
        cobj->drawLine(arg0, arg1, arg2);
        return 0;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {} \n", "ax.DrawNode3D:drawLine", argc, 3);
    return 0;

#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_drawLine'.", &tolua_err);
#endif

    return 0;
}

int lua_cocos2dx_DrawNode3D_clear(lua_State* L)
{
    int argc                  = 0;
    ax::DrawNode3D* cobj = nullptr;
    bool ok                   = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertype(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    cobj = (ax::DrawNode3D*)tolua_tousertype(L, 1, 0);

#if _AX_DEBUG >= 1
    if (!cobj)
    {
        tolua_error(L, "invalid 'cobj' in function 'lua_cocos2dx_DrawNode3D_clear'", nullptr);
        return 0;
    }
#endif

    argc = lua_gettop(L) - 1;
    if (argc == 0)
    {
        if (!ok)
            return 0;
        cobj->clear();
        return 0;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {} \n", "ax.DrawNode3D:clear", argc, 0);
    return 0;

#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_clear'.", &tolua_err);
#endif

    return 0;
}

int lua_cocos2dx_DrawNode3D_drawCube(lua_State* L)
{
    int argc                  = 0;
    ax::DrawNode3D* cobj = nullptr;
    bool ok                   = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertype(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    cobj = (ax::DrawNode3D*)tolua_tousertype(L, 1, 0);

#if _AX_DEBUG >= 1
    if (!cobj)
    {
        tolua_error(L, "invalid 'cobj' in function 'lua_cocos2dx_DrawNode3D_drawCube'", nullptr);
        return 0;
    }
#endif

    argc = lua_gettop(L) - 1;
    if (argc == 2)
    {
        std::vector<ax::Vec3> arg0;
        ax::Color4F arg1;
        Vec3 vec3;
#if _AX_DEBUG >= 1
        if (!tolua_istable(L, 2, 0, &tolua_err))
            goto tolua_lerror;
#endif
        size_t size = lua_objlen(L, 2);
        for (int i = 0; i < size; i++)
        {
            lua_pushnumber(L, i + 1);
            lua_gettable(L, 2);
#if _AX_DEBUG >= 1
            if (!tolua_istable(L, -1, 0, &tolua_err))
            {
                lua_pop(L, 1);
                goto tolua_lerror;
            }
#endif
            ok &= luaval_to_vec3(L, lua_gettop(L), &vec3);

#if _AX_DEBUG >= 1
            if (!ok)
            {
                lua_pop(L, 1);
                goto tolua_lerror;
            }
#endif
            // arg0[i] = vec3;
            arg0.emplace_back(vec3);
            lua_pop(L, 1);
        }

        ok &= luaval_to_color4f(L, 3, &arg1, "ax.DrawNode3D:drawCube");
        if (!ok)
            return 0;
        cobj->drawCube(&arg0[0], arg1);
        return 0;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {} \n", "ax.DrawNode3D:drawCube", argc, 2);
    return 0;

#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_drawCube'.", &tolua_err);
#endif

    return 0;
}

int lua_cocos2dx_DrawNode3D_create(lua_State* L)
{
    int argc = 0;
    bool ok  = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertable(L, 1, "ax.DrawNode3D", 0, &tolua_err))
        goto tolua_lerror;
#endif

    argc = lua_gettop(L) - 1;

    if (argc == 0)
    {
        if (!ok)
            return 0;
        ax::DrawNode3D* ret = ax::DrawNode3D::create();
        object_to_luaval<ax::DrawNode3D>(L, "ax.DrawNode3D", (ax::DrawNode3D*)ret);
        return 1;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {}\n ", "ax.DrawNode3D:create", argc, 0);
    return 0;
#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_DrawNode3D_create'.", &tolua_err);
#endif
    return 0;
}

int lua_register_cocos2dx_DrawNode3D(lua_State* L)
{
    tolua_usertype(L, "ax.DrawNode3D");
    tolua_cclass(L, "DrawNode3D", "ax.DrawNode3D", "ax.Node", nullptr);

    tolua_beginmodule(L, "DrawNode3D");
    tolua_function(L, "getBlendFunc", lua_cocos2dx_DrawNode3D_getBlendFunc);
    tolua_function(L, "drawLine", lua_cocos2dx_DrawNode3D_drawLine);
    tolua_function(L, "clear", lua_cocos2dx_DrawNode3D_clear);
    tolua_function(L, "drawCube", lua_cocos2dx_DrawNode3D_drawCube);
    tolua_function(L, "create", lua_cocos2dx_DrawNode3D_create);
    tolua_endmodule(L);
    auto typeName                                    = typeid(ax::DrawNode3D).name();
    g_luaType[reinterpret_cast<uintptr_t>(typeName)] = "ax.DrawNode3D";
    g_typeCast[typeName]                             = "ax.DrawNode3D";
    return 1;
}

int lua_cocos2dx_ValueTypeJudgeInTable_create(lua_State* L)
{
    int argc = 0;
    bool ok  = true;

#if _AX_DEBUG >= 1
    tolua_Error tolua_err;
#endif

#if _AX_DEBUG >= 1
    if (!tolua_isusertable(L, 1, "ax.ValueTypeJudgeInTable", 0, &tolua_err))
        goto tolua_lerror;
#endif

    argc = lua_gettop(L) - 1;

    if (argc == 1)
    {
        ax::ValueMap arg0;
        ok &= luaval_to_ccvaluemap(L, 2, &arg0, "ax.ValueTypeJudgeInTable:create");
        if (!ok)
            return 0;
        ax::ValueTypeJudgeInTable* ret = ax::ValueTypeJudgeInTable::create(arg0);
        object_to_luaval<ax::ValueTypeJudgeInTable>(L, "ax.ValueTypeJudgeInTable",
                                                         (ax::ValueTypeJudgeInTable*)ret);
        return 1;
    }
    AXLOGD("{} has wrong number of arguments: {}, was expecting {}\n ", "ax.ValueTypeJudgeInTable:create", argc, 1);
    return 0;
#if _AX_DEBUG >= 1
tolua_lerror:
    tolua_error(L, "#ferror in function 'lua_cocos2dx_ValueTypeJudgeInTable_create'.", &tolua_err);
#endif
    return 0;
}

int lua_register_cocos2dx_ValueTypeJudgeInTable(lua_State* L)
{
    tolua_usertype(L, "ax.ValueTypeJudgeInTable");
    tolua_cclass(L, "ValueTypeJudgeInTable", "ax.ValueTypeJudgeInTable", "ax.Node", nullptr);

    tolua_beginmodule(L, "ValueTypeJudgeInTable");
    tolua_function(L, "create", lua_cocos2dx_ValueTypeJudgeInTable_create);
    tolua_endmodule(L);
    auto typeName                                    = typeid(ax::ValueTypeJudgeInTable).name();
    g_luaType[reinterpret_cast<uintptr_t>(typeName)] = "ax.ValueTypeJudgeInTable";
    g_typeCast[typeName]                             = "ax.ValueTypeJudgeInTable";
    return 1;
}

int register_test_binding(lua_State* L)
{
    tolua_open(L);
    tolua_module(L, "ax", 0);
    tolua_beginmodule(L, "ax");
    lua_register_cocos2dx_DrawNode3D(L);
    lua_register_cocos2dx_ValueTypeJudgeInTable(L);
    tolua_endmodule(L);
    return 0;
}
