#include "TApiOpengl.h"
#include "SoyOpenglWindow.h"
#include "TApiCommon.h"

using namespace v8;

const char DrawQuad_FunctionName[] = "DrawQuad";
const char ClearColour_FunctionName[] = "ClearColour";
const char SetViewport_FunctionName[] = "SetViewport";
const char SetUniform_FunctionName[] = "SetUniform";
const char Render_FunctionName[] = "Render";
const char RenderChain_FunctionName[] = "RenderChain";
const char Execute_FunctionName[] = "Execute";
const char GetEnums_FunctionName[] = "GetEnums";

#define DECLARE_IMMEDIATE_FUNC_NAME(NAME)	\
const char Immediate_##NAME##_FunctionName[] = #NAME

#define DEFINE_IMMEDIATE(NAME)	DECLARE_IMMEDIATE_FUNC_NAME(NAME)
DEFINE_IMMEDIATE(disable);
DEFINE_IMMEDIATE(enable);
DEFINE_IMMEDIATE(cullFace);
DEFINE_IMMEDIATE(bindBuffer);
DEFINE_IMMEDIATE(bufferData);
DEFINE_IMMEDIATE(bindFramebuffer);
DEFINE_IMMEDIATE(framebufferTexture2D);
DEFINE_IMMEDIATE(bindTexture);
DEFINE_IMMEDIATE(texImage2D);
DEFINE_IMMEDIATE(useProgram);
DEFINE_IMMEDIATE(texParameteri);
DEFINE_IMMEDIATE(attachShader);
DEFINE_IMMEDIATE(vertexAttribPointer);
DEFINE_IMMEDIATE(enableVertexAttribArray);
DEFINE_IMMEDIATE(setUniform);
DEFINE_IMMEDIATE(texSubImage2D);
DEFINE_IMMEDIATE(readPixels);
DEFINE_IMMEDIATE(viewport);
DEFINE_IMMEDIATE(scissor);
DEFINE_IMMEDIATE(activeTexture);
DEFINE_IMMEDIATE(drawElements);
#undef DEFINE_IMMEDIATE


void ApiOpengl::Bind(TV8Container& Container)
{
	Container.BindObjectType("OpenglWindow", TWindowWrapper::CreateTemplate );
	Container.BindObjectType("OpenglShader", TShaderWrapper::CreateTemplate );
}


TWindowWrapper::~TWindowWrapper()
{
	if ( mWindow )
	{
		mWindow->WaitToFinish();
		mWindow.reset();
	}
}

void TWindowWrapper::OnRender(Opengl::TRenderTarget& RenderTarget)
{
	mWindow->Clear( RenderTarget );
	
	//  call javascript
	TV8Container& Container = *mContainer;
	auto Runner = [&](Local<Context> context)
	{
		auto* isolate = context->GetIsolate();
		auto This = Local<Object>::New( isolate, this->mHandle );
		Container.ExecuteFunc( context, "OnRender", This );
	};
	Container.RunScoped( Runner );
}


void TWindowWrapper::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	using namespace v8;
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	if ( Arguments.Length() != 1 )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "missing arg 0 (window name)"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	auto This = Arguments.This();
	auto& Container = v8::GetObject<TV8Container>( Arguments.Data() );
	
	
	String::Utf8Value WindowName( Arguments[0] );
	std::Debug << "Window Wrapper constructor (" << *WindowName << ")" << std::endl;
	
	//	alloc window
	//	gr: this should be OWNED by the context (so we can destroy all c++ objects with the context)
	//		but it also needs to know of the V8container to run stuff
	//		cyclic hell!
	auto* NewWindow = new TWindowWrapper();
	
	TOpenglParams Params;
	Params.mDoubleBuffer = false;
	Params.mAutoRedraw = false;
	NewWindow->mWindow.reset( new TRenderWindow( *WindowName, Params ) );
	
	//	store persistent handle to the javascript object
	NewWindow->mHandle.Reset( Isolate, Arguments.This() );
	
	NewWindow->mContainer = &Container;
	
	auto OnRender = [NewWindow](Opengl::TRenderTarget& RenderTarget)
	{
		NewWindow->OnRender( RenderTarget );
	};
	NewWindow->mWindow->mOnRender.AddListener( OnRender );
	
	//	set fields
	This->SetInternalField( 0, External::New( Arguments.GetIsolate(), NewWindow ) );
	
	// return the new object back to the javascript caller
	Arguments.GetReturnValue().Set( This );
}

v8::Local<v8::Value> TWindowWrapper::DrawQuad(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;

	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );
	
	if ( Arguments.Length() >= 1 )
	{
		auto ShaderHandle = Arguments[0];
		auto& Shader = TShaderWrapper::Get( Arguments[0] );

		auto OnShaderBindHandle = Arguments[1];
		std::function<void()> OnShaderBind = []{};
		if ( !OnShaderBindHandle->IsUndefined() )
		{
			auto OnShaderBindHandleFunc = v8::Local<Function>::Cast( OnShaderBindHandle );
			OnShaderBind = [&]
			{
				auto OnShaderBindThis = Params.mContext->Global();
				BufferArray<Local<Value>,1> Args;
				Args.PushBack( ShaderHandle );
				Params.mContainer.ExecuteFunc( Params.mContext, OnShaderBindHandleFunc, OnShaderBindThis, GetArrayBridge(Args) );
			};
		}
		
		This.mWindow->DrawQuad( *Shader.mShader, OnShaderBind );
	}
	else
	{
		This.mWindow->DrawQuad();
	}
	
	return v8::Undefined(Params.mIsolate);
}


v8::Local<v8::Value> TWindowWrapper::ClearColour(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );
	
	if ( Arguments.Length() != 3 )
		throw Soy::AssertException("Expecting 3 arguments for ClearColour(r,g,b)");

	auto Red = Local<Number>::Cast( Arguments[0] );
	auto Green = Local<Number>::Cast( Arguments[1] );
	auto Blue = Local<Number>::Cast( Arguments[2] );
	Soy::TRgb Colour( Red->Value(), Green->Value(), Blue->Value() );
		
	This.mWindow->ClearColour( Colour );
	return v8::Undefined(Params.mIsolate);
}


v8::Local<v8::Value> TWindowWrapper::SetViewport(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );

	BufferArray<float,4> Viewportxywh;
	v8::EnumArray( Arguments[0], GetArrayBridge(Viewportxywh), "SetViewport" );
	Soy::Rectf ViewportRect( Viewportxywh[0], Viewportxywh[1], Viewportxywh[2], Viewportxywh[3] );
	
	if ( !This.mActiveRenderTarget )
		throw Soy::AssertException("No active render target");
	
	This.mActiveRenderTarget->SetViewportNormalised( ViewportRect );
	
	return v8::Undefined(Params.mIsolate);
}


template<typename TYPE>
v8::Persistent<TYPE,CopyablePersistentTraits<TYPE>> MakeLocal(v8::Isolate* Isolate,Local<TYPE> LocalHandle)
{
	Persistent<TYPE,CopyablePersistentTraits<TYPE>> PersistentHandle;
	PersistentHandle.Reset( Isolate, LocalHandle );
	return PersistentHandle;
}

v8::Local<v8::Value> TWindowWrapper::Render(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );
	auto* Isolate = Params.mIsolate;

	auto* pThis = &This;
	auto Window = Arguments.This();
	auto WindowPersistent = v8::GetPersistent( *Isolate, Window );
	
	//	make a promise resolver (persistent to copy to thread)
	auto Resolver = v8::Promise::Resolver::New( Isolate );
	auto ResolverPersistent = v8::GetPersistent( *Isolate, Resolver );

	auto TargetPersistent = v8::GetPersistent( *Isolate, Arguments[0] );
	auto* TargetImage = &v8::GetObject<TImageWrapper>(Arguments[0]);
	auto RenderCallbackPersistent = v8::GetPersistent( *Isolate, Arguments[1] );
	bool ReadBackPixelsAfterwards = false;
	if ( Arguments[2]->IsBoolean() )
		ReadBackPixelsAfterwards = Local<Number>::Cast(Arguments[2])->BooleanValue();
	else if ( !Arguments[2]->IsUndefined() )
		throw Soy::AssertException("3rd argument(ReadBackPixels) must be bool or undefined.");
	auto* Container = &Params.mContainer;
	
	auto ExecuteRenderCallback = [=](Local<v8::Context> Context)
	{
		auto* Isolate = Container->mIsolate;
		BufferArray<v8::Local<v8::Value>,2> CallbackParams;
		auto WindowLocal = v8::GetLocal( *Isolate, WindowPersistent );
		auto TargetLocal = v8::GetLocal( *Isolate, TargetPersistent );
		CallbackParams.PushBack( WindowLocal );
		CallbackParams.PushBack( TargetLocal );
		auto CallbackFunctionLocal = v8::GetLocal( *Isolate, RenderCallbackPersistent );
		auto CallbackFunctionLocalFunc = v8::Local<Function>::Cast( CallbackFunctionLocal );
		auto FunctionThis = Context->Global();
		Container->ExecuteFunc( Context, CallbackFunctionLocalFunc, FunctionThis, GetArrayBridge(CallbackParams) );
	};
	
	auto OnCompleted = [=](Local<Context> Context)
	{
		//	gr: can't do this unless we're in the javascript thread...
		auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
		auto Message = String::NewFromUtf8( Isolate, "Yay!");
		ResolverLocal->Resolve( Message );
	};
	
	auto OpenglRender = [=]
	{
		try
		{
			//	get the texture from the image
			std::string GenerateTextureError;
			auto OnError = [&](const std::string& Error)
			{
				throw Soy::AssertException(Error);
			};
			TargetImage->GetTexture( []{}, OnError );
		
			//	setup render target
			auto& TargetTexture = TargetImage->GetTexture();
			Opengl::TRenderTargetFbo RenderTarget( TargetTexture );
			RenderTarget.mGenerateMipMaps = false;
			RenderTarget.Bind();
			
			//	hack! need to turn render target into it's own javasript object
			pThis->mActiveRenderTarget = &RenderTarget;
			RenderTarget.SetViewportNormalised( Soy::Rectf(0,0,1,1) );
			try
			{
				//	immediately call the javascript callback
				Container->RunScoped( ExecuteRenderCallback );
				pThis->mActiveRenderTarget = nullptr;
				RenderTarget.Unbind();
			}
			catch(std::exception& e)
			{
				pThis->mActiveRenderTarget = nullptr;
				RenderTarget.Unbind();
				throw;
			}

			TargetImage->OnOpenglTextureChanged();
			if ( ReadBackPixelsAfterwards )
			{
				TargetImage->ReadOpenglPixels();
			}

			//	queue the completion, doesn't need to be done instantly
			Container->QueueScoped( OnCompleted );
		}
		catch(std::exception& e)
		{
			//	queue the error callback
			std::string ExceptionString(e.what());
			auto OnError = [=](Local<Context> Context)
			{
				auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
				//	gr: does this need to be an exception? string?
				auto Error = String::NewFromUtf8( Isolate, ExceptionString.c_str() );
				//auto Exception = v8::GetException( *Context->GetIsolate(), ExceptionString)
				//ResolverLocal->Reject( Exception );
				ResolverLocal->Reject( Error );
			};
			Container->QueueScoped( OnError );
		}
	};
	auto& OpenglContext = *This.mWindow->GetContext();
	OpenglContext.PushJob( OpenglRender );

	//	return the promise
	auto Promise = Resolver->GetPromise();
	return Promise;
}



v8::Local<v8::Value> TWindowWrapper::RenderChain(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );
	auto* Isolate = Params.mIsolate;
	
	auto* pThis = &This;
	auto Window = Arguments.This();
	auto WindowPersistent = v8::GetPersistent( *Isolate, Window );
	
	//	make a promise resolver (persistent to copy to thread)
	auto Resolver = v8::Promise::Resolver::New( Isolate );
	auto ResolverPersistent = v8::GetPersistent( *Isolate, Resolver );
	
	auto TargetPersistent = v8::GetPersistent( *Isolate, Arguments[0] );
	auto* TargetImage = &v8::GetObject<TImageWrapper>(Arguments[0]);
	auto RenderCallbackPersistent = v8::GetPersistent( *Isolate, Arguments[1] );
	bool ReadBackPixelsAfterwards = false;
	if ( Arguments[2]->IsBoolean() )
		ReadBackPixelsAfterwards = Local<Number>::Cast(Arguments[2])->BooleanValue();
	else if ( !Arguments[2]->IsUndefined() )
		throw Soy::AssertException("3rd argument(ReadBackPixels) must be bool or undefined.");
	auto TempPersistent = v8::GetPersistent( *Isolate, Arguments[3] );
	auto* TempImage = &v8::GetObject<TImageWrapper>(Arguments[3]);
	auto IterationCount = Local<Number>::Cast( Arguments[4] )->Int32Value();
	auto* Container = &Params.mContainer;
	
	auto OnCompleted = [=](Local<Context> Context)
	{
		//	gr: can't do this unless we're in the javascript thread...
		auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
		auto Message = String::NewFromUtf8( Isolate, "Yay!");
		ResolverLocal->Resolve( Message );
	};
	
	
	auto OpenglRender = [=]
	{
		try
		{
			//	get the texture from the image
			std::string GenerateTextureError;
			auto OnError = [&](const std::string& Error)
			{
				throw Soy::AssertException(Error);
			};
			TargetImage->GetTexture( []{}, OnError );
			TempImage->GetTexture( []{}, OnError );
			
			//	targets for chain
			auto& FinalTargetTexture = TargetImage->GetTexture();
			auto& TempTargetTexture = TempImage->GetTexture();
			
			//	do back/front buffer order so FinalTarget is always last front-target
			BufferArray<TImageWrapper*,2> Targets;
			if ( IterationCount % 2 == 1 )
			{
				Targets.PushBack( TempImage );
				Targets.PushBack( TargetImage );
			}
			else
			{
				Targets.PushBack( TargetImage );
				Targets.PushBack( TempImage );
			}
			
			for ( int it=0;	it<IterationCount;	it++ )
			{
				auto& PreviousBuffer = *Targets[ (it+0) % Targets.GetSize() ];
				auto& CurrentBuffer = *Targets[ (it+1) % Targets.GetSize() ];
				
				Opengl::TRenderTargetFbo RenderTarget( CurrentBuffer.GetTexture() );
				RenderTarget.mGenerateMipMaps = false;
				RenderTarget.Bind();
				pThis->mActiveRenderTarget = &RenderTarget;
				RenderTarget.SetViewportNormalised( Soy::Rectf(0,0,1,1) );
				try
				{
					auto ExecuteRenderCallback = [=](Local<v8::Context> Context)
					{
						auto* Isolate = Container->mIsolate;
						BufferArray<v8::Local<v8::Value>,4> CallbackParams;
						auto WindowLocal = v8::GetLocal( *Isolate, WindowPersistent );
						auto CurrentLocal = v8::GetLocal( *Isolate, CurrentBuffer.mHandle );
						auto PreviousLocal = v8::GetLocal( *Isolate, PreviousBuffer.mHandle );
						auto IterationLocal = Number::New( Isolate, it );
						CallbackParams.PushBack( WindowLocal );
						CallbackParams.PushBack( CurrentLocal );
						CallbackParams.PushBack( PreviousLocal );
						CallbackParams.PushBack( IterationLocal );
						auto CallbackFunctionLocal = v8::GetLocal( *Isolate, RenderCallbackPersistent );
						auto CallbackFunctionLocalFunc = v8::Local<Function>::Cast( CallbackFunctionLocal );
						auto FunctionThis = Context->Global();
						Container->ExecuteFunc( Context, CallbackFunctionLocalFunc, FunctionThis, GetArrayBridge(CallbackParams) );
					};
					
					//	immediately call the javascript callback
					Container->RunScoped( ExecuteRenderCallback );
					pThis->mActiveRenderTarget = nullptr;
					RenderTarget.Unbind();
					CurrentBuffer.OnOpenglTextureChanged();
				}
				catch(std::exception& e)
				{
					pThis->mActiveRenderTarget = nullptr;
					RenderTarget.Unbind();
					throw;
				}
				
			}

			if ( ReadBackPixelsAfterwards )
			{
				TargetImage->ReadOpenglPixels();
			}
			
			//	queue the completion, doesn't need to be done instantly
			Container->QueueScoped( OnCompleted );
		}
		catch(std::exception& e)
		{
			//	queue the error callback
			std::string ExceptionString(e.what());
			auto OnError = [=](Local<Context> Context)
			{
				auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
				//	gr: does this need to be an exception? string?
				auto Error = String::NewFromUtf8( Isolate, ExceptionString.c_str() );
				//auto Exception = v8::GetException( *Context->GetIsolate(), ExceptionString)
				//ResolverLocal->Reject( Exception );
				ResolverLocal->Reject( Error );
			};
			Container->QueueScoped( OnError );
		}
	};
	auto& OpenglContext = *This.mWindow->GetContext();
	OpenglContext.PushJob( OpenglRender );
	
	//	return the promise
	auto Promise = Resolver->GetPromise();
	return Promise;
}



v8::Local<v8::Value> TWindowWrapper::Execute(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.This() );
	auto* Isolate = Params.mIsolate;
	
	auto Window = Arguments.This();
	auto WindowPersistent = v8::GetPersistent( *Isolate, Window );
	
	//	make a promise resolver (persistent to copy to thread)
	auto Resolver = v8::Promise::Resolver::New( Isolate );
	auto ResolverPersistent = v8::GetPersistent( *Isolate, Resolver );
	
	bool StealThread = false;
	if ( Arguments[1]->IsBoolean() )
		StealThread = Local<Number>::Cast(Arguments[1])->BooleanValue();
	else if ( !Arguments[1]->IsUndefined() )
		throw Soy::AssertException("2nd argument(StealThread) must be bool or undefined (default to false).");

	auto RenderCallbackPersistent = v8::GetPersistent( *Isolate, Arguments[0] );
	auto* Container = &Params.mContainer;
	
	auto ExecuteRenderCallback = [=](Local<v8::Context> Context)
	{
		auto* Isolate = Container->mIsolate;
		BufferArray<v8::Local<v8::Value>,2> CallbackParams;
		auto WindowLocal = v8::GetLocal( *Isolate, WindowPersistent );
		CallbackParams.PushBack( WindowLocal );
		auto CallbackFunctionLocal = v8::GetLocal( *Isolate, RenderCallbackPersistent );
		auto CallbackFunctionLocalFunc = v8::Local<Function>::Cast( CallbackFunctionLocal );
		auto FunctionThis = Context->Global();
		Container->ExecuteFunc( Context, CallbackFunctionLocalFunc, FunctionThis, GetArrayBridge(CallbackParams) );
	};
	
	auto OnCompleted = [=](Local<Context> Context)
	{
		//	gr: can't do this unless we're in the javascript thread...
		auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
		auto Message = String::NewFromUtf8( Isolate, "Yay!");
		ResolverLocal->Resolve( Message );
	};
	
	auto OpenglRender = [=]
	{
		try
		{
			//	immediately call the javascript callback
			Container->RunScoped( ExecuteRenderCallback );
			
			//	queue the completion, doesn't need to be done instantly
			Container->QueueScoped( OnCompleted );
		}
		catch(std::exception& e)
		{
			//	queue the error callback
			std::string ExceptionString(e.what());
			auto OnError = [=](Local<Context> Context)
			{
				auto ResolverLocal = v8::GetLocal( *Isolate, ResolverPersistent );
				//	gr: does this need to be an exception? string?
				auto Error = String::NewFromUtf8( Isolate, ExceptionString.c_str() );
				//auto Exception = v8::GetException( *Context->GetIsolate(), ExceptionString)
				//ResolverLocal->Reject( Exception );
				ResolverLocal->Reject( Error );
			};
			Container->QueueScoped( OnError );
		}
	};
	auto& OpenglContext = *This.mWindow->GetContext();
	
	if ( StealThread )
	{
		if ( !OpenglContext.Lock() )
			throw Soy::AssertException("Failed to steal thread (lock context)");
		
		try
		{
			//	immediately call the javascript callback
			Container->RunScoped( ExecuteRenderCallback );
			OpenglContext.Unlock();
		}
		catch(...)
		{
			OpenglContext.Unlock();
			throw;
		}
		return v8::Undefined(Params.mIsolate);
	}
	else
	{
		OpenglContext.PushJob( OpenglRender );
	
		//	return the promise
		auto Promise = Resolver->GetPromise();
		return Promise;
	}
}



v8::Local<v8::Value> TWindowWrapper::GetEnums(const v8::CallbackInfo& Params)
{
	auto* Isolate = &Params.GetIsolate();
	//	make an associative array of opengl enums for immediate use
	auto ArrayHandle = Object::New( Isolate );
	
	auto PushEnum = [&](const char* GlName,uint32_t Value)
	{
		std::string Name( GlName );
		//	strip GL_ off the start
		Soy::StringTrimLeft(Name,"GL_",true);
		
		auto KeyHandle = v8::GetString( *Isolate, Name );
		auto ValueHandle = Number::New( Isolate, Value );
		ArrayHandle->Set( KeyHandle, ValueHandle );
	};
#define PUSH_ENUM(NAME)	PushEnum( #NAME, NAME )
	PUSH_ENUM( GL_TEXTURE_2D );
	PUSH_ENUM( GL_DEPTH_TEST );
	PUSH_ENUM( GL_STENCIL_TEST );
	PUSH_ENUM( GL_BLEND );
	PUSH_ENUM( GL_DITHER );
	PUSH_ENUM( GL_POLYGON_OFFSET_FILL );
	PUSH_ENUM( GL_SAMPLE_COVERAGE );
	PUSH_ENUM( GL_SCISSOR_TEST );
	PUSH_ENUM( GL_FRAMEBUFFER_COMPLETE );
	PUSH_ENUM( GL_CULL_FACE );
	PUSH_ENUM( GL_BACK );
	PUSH_ENUM( GL_TEXTURE_WRAP_S );
	PUSH_ENUM( GL_CLAMP_TO_EDGE );
	PUSH_ENUM( GL_TEXTURE_WRAP_T );
	PUSH_ENUM( GL_CLAMP_TO_EDGE );
	PUSH_ENUM( GL_TEXTURE_MIN_FILTER );
	PUSH_ENUM( GL_NEAREST );
	PUSH_ENUM( GL_TEXTURE_MAG_FILTER );
	PUSH_ENUM( GL_ARRAY_BUFFER );
	PUSH_ENUM( GL_ELEMENT_ARRAY_BUFFER );
	PUSH_ENUM( GL_FLOAT );
	PUSH_ENUM( GL_STATIC_DRAW );
	PUSH_ENUM( GL_FRAMEBUFFER );
	PUSH_ENUM( GL_RGBA );
	PUSH_ENUM( GL_RGB );
	PUSH_ENUM( GL_RGB32F );
	PUSH_ENUM( GL_RGBA32F );
	PUSH_ENUM( GL_COLOR_ATTACHMENT0 );
	
	return ArrayHandle;
	
}



Local<FunctionTemplate> TWindowWrapper::CreateTemplate(TV8Container& Container)
{
	auto* Isolate = Container.mIsolate;
	
	//	pass the container around
	auto ContainerHandle = External::New( Isolate, &Container );
	auto ConstructorFunc = FunctionTemplate::New( Isolate, Constructor, ContainerHandle );
	
	//	https://github.com/v8/v8/wiki/Embedder's-Guide
	//	1 field to 1 c++ object
	//	gr: we can just use the template that's made automatically and modify that!
	//	gr: prototypetemplate and instancetemplate are basically the same
	//		but for inheritance we may want to use prototype
	//		https://groups.google.com/forum/#!topic/v8-users/_i-3mgG5z-c
	auto InstanceTemplate = ConstructorFunc->InstanceTemplate();
	
	//	[0] object
	//	[1] container
	InstanceTemplate->SetInternalFieldCount(2);
	
	
	//	add members
	Container.BindFunction<DrawQuad_FunctionName>( InstanceTemplate, DrawQuad );
	Container.BindFunction<SetViewport_FunctionName>( InstanceTemplate, SetViewport );
	Container.BindFunction<ClearColour_FunctionName>( InstanceTemplate, ClearColour );
	Container.BindFunction<Render_FunctionName>( InstanceTemplate, Render );
	Container.BindFunction<RenderChain_FunctionName>( InstanceTemplate, RenderChain );
	Container.BindFunction<Execute_FunctionName>( InstanceTemplate, Execute );

#define BIND_IMMEDIATE(NAME)	\
	Container.BindFunction<Immediate_## NAME ##_FunctionName>( InstanceTemplate, Immediate_ ## NAME );{}

#define DEFINE_IMMEDIATE(NAME)	BIND_IMMEDIATE(NAME)
	DEFINE_IMMEDIATE(disable);
	DEFINE_IMMEDIATE(enable);
	DEFINE_IMMEDIATE(cullFace);
	DEFINE_IMMEDIATE(bindBuffer);
	DEFINE_IMMEDIATE(bufferData);
	DEFINE_IMMEDIATE(bindFramebuffer);
	DEFINE_IMMEDIATE(framebufferTexture2D);
	DEFINE_IMMEDIATE(bindTexture);
	DEFINE_IMMEDIATE(texImage2D);
	DEFINE_IMMEDIATE(useProgram);
	DEFINE_IMMEDIATE(texParameteri);
	DEFINE_IMMEDIATE(attachShader);
	DEFINE_IMMEDIATE(vertexAttribPointer);
	DEFINE_IMMEDIATE(enableVertexAttribArray);
	DEFINE_IMMEDIATE(setUniform);
	DEFINE_IMMEDIATE(texSubImage2D);
	DEFINE_IMMEDIATE(readPixels);
	DEFINE_IMMEDIATE(viewport);
	DEFINE_IMMEDIATE(scissor);
	DEFINE_IMMEDIATE(activeTexture);
	DEFINE_IMMEDIATE(drawElements);
#undef DEFINE_IMMEDIATE
	
	Container.BindFunction<GetEnums_FunctionName>( InstanceTemplate, GetEnums );

	//point_templ.SetAccessor(String::NewFromUtf8(isolate, "x"), GetPointX, SetPointX);
	//point_templ.SetAccessor(String::NewFromUtf8(isolate, "y"), GetPointY, SetPointY);
	
	return ConstructorFunc;
}


template<typename TYPE>
TYPE GetGlValue(Local<Value> Argument);

template<>
GLenum GetGlValue(Local<Value> Argument)
{
	if ( !Argument->IsNumber() )
	{
		std::stringstream Error;
		Error << "Expecting argument as number, but is " << v8::GetTypeName(Argument);
		if ( Argument->IsString() )
			Error << " (" << v8::GetString(Argument) << ")";
		throw Soy::AssertException(Error.str());
	}

	auto Value32 = Argument.As<Number>()->Uint32Value();
	auto Value = size_cast<GLenum>(Value32);
	return Value;
}

template<>
GLint GetGlValue(Local<Value> Argument)
{
	if ( !Argument->IsNumber() )
	{
		std::stringstream Error;
		Error << "Expecting argument as number, but is " << v8::GetTypeName(Argument);
		if ( Argument->IsString() )
			Error << " (" << v8::GetString(Argument) << ")";
		throw Soy::AssertException(Error.str());
	}
	
	auto Value32 = Argument.As<Number>()->Int32Value();
	auto Value = size_cast<GLint>(Value32);
	return Value;
}

/*
template<>
GLsizeiptr GetGlValue(Local<Value> Argument)
{
	return 0;
}

template<>
const GLvoid* GetGlValue(Local<Value> Argument)
{
	return nullptr;
}
*/


template<typename RETURN,typename ARG0,typename ARG1>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0,ARG1),const v8::CallbackInfo& Arguments,const ARG0& Arg0,const ARG0& Arg1)
{
	std::Debug << Context << std::endl;
	FunctionPtr( Arg0, Arg1 );
	Opengl::IsOkay(Context);
	return v8::Undefined(&Arguments.GetIsolate());
}

template<typename RETURN,typename ARG0>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0),const v8::CallbackInfo& Arguments)
{
	std::Debug << Context << std::endl;
	auto Arg0 = GetGlValue<ARG0>( Arguments.mParams[0] );
	FunctionPtr( Arg0 );
	Opengl::IsOkay(Context);
	return v8::Undefined(&Arguments.GetIsolate());
}

template<typename RETURN,typename ARG0,typename ARG1>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0,ARG1),const v8::CallbackInfo& Arguments)
{
	auto Arg0 = GetGlValue<ARG0>( Arguments.mParams[0] );
	auto Arg1 = GetGlValue<ARG1>( Arguments.mParams[1] );
	return Immediate_Func( Context, FunctionPtr, Arguments, Arg0, Arg1 );
}

template<typename RETURN,typename ARG0,typename ARG1,typename ARG2>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0,ARG1,ARG2),const v8::CallbackInfo& Arguments)
{
	std::Debug << Context << std::endl;
	auto Arg0 = GetGlValue<ARG0>( Arguments.mParams[0] );
	auto Arg1 = GetGlValue<ARG1>( Arguments.mParams[1] );
	auto Arg2 = GetGlValue<ARG2>( Arguments.mParams[2] );
	FunctionPtr( Arg0, Arg1, Arg2 );
	Opengl::IsOkay(Context);
	return v8::Undefined(&Arguments.GetIsolate());
}

template<typename RETURN,typename ARG0,typename ARG1,typename ARG2,typename ARG3>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0,ARG1,ARG2,ARG3),const v8::CallbackInfo& Arguments)
{
	std::Debug << Context << std::endl;
	auto Arg0 = GetGlValue<ARG0>( Arguments.mParams[0] );
	auto Arg1 = GetGlValue<ARG1>( Arguments.mParams[1] );
	auto Arg2 = GetGlValue<ARG2>( Arguments.mParams[2] );
	auto Arg3 = GetGlValue<ARG3>( Arguments.mParams[3] );
	FunctionPtr( Arg0, Arg1, Arg2, Arg3 );
	Opengl::IsOkay(Context);
	return v8::Undefined(&Arguments.GetIsolate());
}

template<typename RETURN,typename ARG0,typename ARG1,typename ARG2,typename ARG3,typename ARG4>
v8::Local<v8::Value> Immediate_Func(const char* Context,RETURN(*FunctionPtr)(ARG0,ARG1,ARG2,ARG3,ARG4),const v8::CallbackInfo& Arguments)
{
	std::Debug << Context << std::endl;
	auto Arg0 = GetGlValue<ARG0>( Arguments.mParams[0] );
	auto Arg1 = GetGlValue<ARG1>( Arguments.mParams[1] );
	auto Arg2 = GetGlValue<ARG2>( Arguments.mParams[2] );
	auto Arg3 = GetGlValue<ARG3>( Arguments.mParams[3] );
	auto Arg4 = GetGlValue<ARG4>( Arguments.mParams[4] );
	FunctionPtr( Arg0, Arg1, Arg2, Arg3, Arg4 );
	Opengl::IsOkay(Context);
	return v8::Undefined(&Arguments.GetIsolate());
}



v8::Local<v8::Value> TWindowWrapper::Immediate_disable(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glDisable", glDisable, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_enable(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glEnable", glEnable, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_cullFace(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glCullFace", glCullFace, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_bindBuffer(const v8::CallbackInfo& Arguments)
{
	//	gr; buffers are allocated as-required, high-level they're just an id
	auto Binding = GetGlValue<GLenum>( Arguments.mParams[0] );
	auto BufferNameJs = GetGlValue<int>( Arguments.mParams[1] );
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.mParams.This() );

	//	get/alloc our instance
	auto BufferName = This.mImmediateObjects.GetBuffer(BufferNameJs).mName;
	//	gr: glIsBuffer check is useless here, but we can do it immediate after
	//A name returned by glGenBuffers, but not yet associated with a buffer object by calling glBindBuffer, is not the name of a buffer object.
	
	auto Return = Immediate_Func( "glBindBuffer", glBindBuffer, Arguments, Binding, BufferName );
	
	//	if bound, then NOW is should be valid
	if ( !glIsBuffer( BufferName ) )
	{
		std::stringstream Error;
		Error << BufferNameJs << "(" << BufferName << ") is not a valid buffer id";
		throw Soy::AssertException(Error.str());
	}
	return Return;
}

v8::Local<v8::Value> TWindowWrapper::Immediate_bufferData(const v8::CallbackInfo& Arguments)
{
	auto target = GetGlValue<GLenum>( Arguments.mParams[0] );
	auto usage = GetGlValue<GLenum>( Arguments.mParams[2] );
	auto Arg1 = Arguments.mParams[1];

	if ( Arg1->IsNumber() )
	{
		//	uninitialised but allocated buffer
		auto SizeValue = Arg1.As<Number>();
		auto size = SizeValue->Uint32Value();
		glBufferData( target, size, nullptr, usage );
	}
	//	gr: don't currently know if I can send a mix of ints and floats to gl buffer data for different things (vertex vs index)
	//	so for now explicity get floats or uint16's
	else if ( Arg1->IsFloat32Array() )
	{
		Array<float> Data;
		v8::EnumArray( Arg1, GetArrayBridge(Data), "glBufferData" );
		auto size = Data.GetDataSize();
		glBufferData( target,  Data.GetDataSize(), Data.GetArray(), usage );
	}
	else if ( Arg1->IsUint16Array() )
	{
		Array<uint16_t> Data;
		EnumArray<Uint16Array>( Arg1, GetArrayBridge(Data) );
		//v8::EnumArray( Arg1, GetArrayBridge(Data), "glBufferData" );
		auto size = Data.GetDataSize();
		glBufferData( target,  Data.GetDataSize(), Data.GetArray(), usage );
	}
	else
	{
		std::stringstream Error;
		Error << "Not yet handling " << v8::GetTypeName(Arg1) << " for glBufferData. Need to work out what we're allowed to send";
		throw Soy::AssertException(Error.str());
	}
	
	Opengl::IsOkay("glBufferData");
	return v8::Undefined( Arguments.mIsolate );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_bindFramebuffer(const v8::CallbackInfo& Arguments)
{
	//	gr; buffers are allocated as-required, high-level they're just an id
	auto Binding = GetGlValue<GLenum>( Arguments.mParams[0] );
	auto BufferNameJs = GetGlValue<int>( Arguments.mParams[1] );
	auto& This = v8::GetObject<TWindowWrapper>( Arguments.mParams.This() );
	
	//	get/alloc our instance
	auto BufferName = This.mImmediateObjects.GetFrameBuffer(BufferNameJs).mName;
	//	gr: glIsBuffer check is useless here, but we can do it immediate after
	//A name returned by glGenBuffers, but not yet associated with a buffer object by calling glBindBuffer, is not the name of a buffer object.
	
	auto Return = Immediate_Func( "glBindFramebuffer", glBindFramebuffer, Arguments, Binding, BufferName );
	
	//	if bound, then NOW is should be valid
	if ( !glIsFramebuffer( BufferName ) )
	{
		std::stringstream Error;
		Error << BufferNameJs << "(" << BufferName << ") is not a valid Framebuffer id";
		throw Soy::AssertException(Error.str());
	}
	return Return;
}

v8::Local<v8::Value> TWindowWrapper::Immediate_framebufferTexture2D(const v8::CallbackInfo& Arguments)
{
	//GLAPI void APIENTRY glFramebufferTexture2D (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
	auto target = GetGlValue<GLenum>( Arguments.mParams[0] );
	auto attachment = GetGlValue<GLenum>( Arguments.mParams[1] );
	auto textarget = GetGlValue<GLenum>( Arguments.mParams[2] );
	auto TextureHandle = Arguments.mParams[3];
	auto level = GetGlValue<GLint>( Arguments.mParams[4] );
	
	auto& Image = v8::GetObject<TImageWrapper>(TextureHandle);
	auto& Texture = Image.GetTexture();
	auto TextureName = Texture.mTexture.mName;
	
	//GLAPI void APIENTRY glFramebufferTexture2D (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
	glFramebufferTexture2D( target, attachment, textarget, TextureName, level );
	Opengl::IsOkay("glFramebufferTexture2D");
	return v8::Undefined( Arguments.mIsolate );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_bindTexture(const v8::CallbackInfo& Arguments)
{
	auto Binding = GetGlValue<GLenum>( Arguments.mParams[0] );
	
	//	get texture
	auto& Image = v8::GetObject<TImageWrapper>( Arguments.mParams[1] );
	Image.GetTexture( []{}, [](const std::string& Error){} );
	auto& Texture = Image.GetTexture();
	auto TextureName = Texture.mTexture.mName;
	
	return Immediate_Func( "glBindTexture", glBindTexture, Arguments, Binding, TextureName );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_texImage2D(const v8::CallbackInfo& Arguments)
{
	/*
	 https://developer.mozilla.org/en-US/docs/Web/API/WebGLRenderingContext/texImage2D
	 //Webgl1
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, ArrayBufferView? pixels);
	 void gl.texImage2D(target, level, internalformat, format, type, ImageData? pixels);
	 void gl.texImage2D(target, level, internalformat, format, type, HTMLImageElement? pixels);
	 void gl.texImage2D(target, level, internalformat, format, type, HTMLCanvasElement? pixels);
	 void gl.texImage2D(target, level, internalformat, format, type, HTMLVideoElement? pixels);
	 void gl.texImage2D(target, level, internalformat, format, type, ImageBitmap? pixels);
	 
	 // WebGL2:
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, GLintptr offset);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, HTMLCanvasElement source);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, HTMLImageElement source);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, HTMLVideoElement source);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, ImageBitmap source);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, ImageData source);
	 void gl.texImage2D(target, level, internalformat, width, height, border, format, type, ArrayBufferView srcData, srcOffset);
	 */
	//	gr: we're emulating webgl2
	auto binding = GetGlValue<GLenum>( Arguments.mParams[0] );
	auto level = GetGlValue<GLint>( Arguments.mParams[1] );
	auto internalformat = GetGlValue<GLint>( Arguments.mParams[2] );
	auto width = GetGlValue<GLsizei>( Arguments.mParams[3] );
	auto height = GetGlValue<GLsizei>( Arguments.mParams[4] );
	auto border = GetGlValue<GLint>( Arguments.mParams[5] );
	auto externalformat = GetGlValue<GLenum>( Arguments.mParams[6] );
	auto externaltype = GetGlValue<GLenum>( Arguments.mParams[7] );
	auto DataHandle = Arguments.mParams[8];

	void* PixelData = nullptr;
	if ( DataHandle->IsNull() )
	{
		//	this is fine... allocating
	}
	else
	{
		std::stringstream Error;
		Error << "don't know how to handle texImage2D data of " << v8::GetTypeName(DataHandle);
		throw Soy::AssertException(Error.str());
	}
	
	glTexImage2D( binding, level, internalformat, width, height, border, externalformat, externaltype, PixelData );
	Opengl::IsOkay("glTexImage2D");
	return v8::Undefined( Arguments.mIsolate );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_useProgram(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glUseProgram", glUseProgram, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_texParameteri(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glTexParameteri", glTexParameteri, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_attachShader(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glAttachShader", glAttachShader, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_vertexAttribPointer(const v8::CallbackInfo& Arguments)
{
	throw Soy::AssertException("glVertexAttribPointer needs some specifics");
	//return Immediate_Func( glVertexAttribPointer, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_enableVertexAttribArray(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glEnableVertexAttribArray", glEnableVertexAttribArray, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_setUniform(const v8::CallbackInfo& Arguments)
{
	throw Soy::AssertException("Set Uniform needs some specifics");
	//return Immediate_Func( glDisable, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_texSubImage2D(const v8::CallbackInfo& Arguments)
{
	throw Soy::AssertException("glTexSubImage2D needs some specifics");
	//return Immediate_Func( glTexSubImage2D, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_readPixels(const v8::CallbackInfo& Arguments)
{
	throw Soy::AssertException("glReadPixels needs some specifics");
	//return Immediate_Func( glReadPixels, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_viewport(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glViewport", glViewport, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_scissor(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glScissor", glScissor, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_activeTexture(const v8::CallbackInfo& Arguments)
{
	return Immediate_Func( "glActiveTexture", glActiveTexture, Arguments );
}

v8::Local<v8::Value> TWindowWrapper::Immediate_drawElements(const v8::CallbackInfo& Arguments)
{
	throw Soy::AssertException("glDrawElements needs some specifics");
	//return Immediate_Func( "glDrawElements", glDrawElements, Arguments );
}


void TRenderWindow::Clear(Opengl::TRenderTarget &RenderTarget)
{
	auto FrameBufferSize = RenderTarget.GetSize();
	
	Soy::Rectf Viewport(0,0,1,1);
	RenderTarget.SetViewportNormalised( Viewport );
	
	//Opengl::ClearColour( Soy::TRgb(51/255.f,204/255.f,255/255.f) );
	Opengl::ClearDepth();
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	
	
	auto OpenglContext = this->GetContext();
	Opengl_IsOkay();
}


void TRenderWindow::ClearColour(Soy::TRgb Colour)
{
	Opengl::ClearColour( Colour );
}


Opengl::TGeometry& TRenderWindow::GetBlitQuad()
{
	if ( !mBlitQuad )
	{
		//	make mesh
		struct TVertex
		{
			vec2f	uv;
		};
		class TMesh
		{
			public:
			TVertex	mVertexes[4];
		};
		TMesh Mesh;
		Mesh.mVertexes[0].uv = vec2f( 0, 0);
		Mesh.mVertexes[1].uv = vec2f( 1, 0);
		Mesh.mVertexes[2].uv = vec2f( 1, 1);
		Mesh.mVertexes[3].uv = vec2f( 0, 1);
		Array<size_t> Indexes;
		
		Indexes.PushBack( 0 );
		Indexes.PushBack( 1 );
		Indexes.PushBack( 2 );
		
		Indexes.PushBack( 2 );
		Indexes.PushBack( 3 );
		Indexes.PushBack( 0 );
		
		//	for each part of the vertex, add an attribute to describe the overall vertex
		SoyGraphics::TGeometryVertex Vertex;
		auto& UvAttrib = Vertex.mElements.PushBack();
		UvAttrib.mName = "TexCoord";
		UvAttrib.SetType<vec2f>();
		UvAttrib.mIndex = 0;	//	gr: does this matter?
		
		Array<uint8> MeshData;
		MeshData.PushBackReinterpret( Mesh );
		mBlitQuad.reset( new Opengl::TGeometry( GetArrayBridge(MeshData), GetArrayBridge(Indexes), Vertex ) );
	}
	
	return *mBlitQuad;
}

void TRenderWindow::DrawQuad()
{
	//	allocate objects we need!
	if ( !mDebugShader )
	{
		auto& BlitQuad = GetBlitQuad();
		auto& Context = *GetContext();
		
		auto VertShader =
		"#version 410\n"
		//"uniform vec4 Rect;\n"
		"const vec4 Rect = vec4(0,0,1,1);\n"
		"in vec2 TexCoord;\n"
		"out vec2 uv;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(TexCoord.x,TexCoord.y,0,1);\n"
		"   gl_Position.xy *= Rect.zw;\n"
		"   gl_Position.xy += Rect.xy;\n"
		//	move to view space 0..1 to -1..1
		"	gl_Position.xy *= vec2(2,2);\n"
		"	gl_Position.xy -= vec2(1,1);\n"
		"	uv = vec2(TexCoord.x,1-TexCoord.y);\n"
		"}\n";
		auto FragShader =
		"#version 410\n"
		"in vec2 uv;\n"
		//"out vec4 FragColor;\n"
		"void main()\n"
		"{\n"
		"	gl_FragColor = vec4(uv.x,uv.y,0,1);\n"
		"}\n";
		
		mDebugShader.reset( new Opengl::TShader( VertShader, FragShader, BlitQuad.mVertexDescription, "Blit shader", Context ) );
	}
	
	DrawQuad( *mDebugShader, []{} );
}


void TRenderWindow::DrawQuad(Opengl::TShader& Shader,std::function<void()> OnBind)
{
	auto& BlitQuad = GetBlitQuad();
	
	//	do bindings
	auto ShaderBound = Shader.Bind();
	OnBind();
	BlitQuad.Draw();
	Opengl_IsOkay();
}





TShaderWrapper::~TShaderWrapper()
{
	//	todo: opengl deferrefed delete
}


void TShaderWrapper::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	using namespace v8;
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	if ( Arguments.Length() != 3 )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "missing arguments (Window,FragSource,VertSource)"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	auto This = Arguments.This();
	
	//	gr: auto catch this
	try
	{
		auto& Container = v8::GetObject<TV8Container>( Arguments.Data() );
		
		//	access to context!
		auto& Window = TWindowWrapper::Get( Arguments[0] );
		auto& OpenglContext = *Window.mWindow->GetContext();
		String::Utf8Value VertSource( Arguments[1] );
		String::Utf8Value FragSource( Arguments[2] );

		//	this needs to be deffered to be on the opengl thread (or at least wait for context to initialise)
		std::function<Opengl::TGeometry&()> GetGeo = [&Window]()-> Opengl::TGeometry&
		{
			auto& Geo = Window.mWindow->GetBlitQuad();
			return Geo;
		};
		//	gr: this should be OWNED by the context (so we can destroy all c++ objects with the context)
		//		but it also needs to know of the V8container to run stuff
		//		cyclic hell!
		auto* NewShader = new TShaderWrapper();
		NewShader->mHandle.Reset( Isolate, Arguments.This() );
		NewShader->mContainer = &Container;

		NewShader->CreateShader( OpenglContext, GetGeo, *VertSource, *FragSource );
		
		//	set fields
		This->SetInternalField( 0, External::New( Arguments.GetIsolate(), NewShader ) );
		
		// return the new object back to the javascript caller
		Arguments.GetReturnValue().Set( This );
	}
	catch(std::exception& e)
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, e.what() ));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
}


v8::Local<v8::Value> TShaderWrapper::SetUniform(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TShaderWrapper>( ThisHandle );
	auto pShader = This.mShader;
	auto& Shader = *pShader;
	
	auto* UniformName = *String::Utf8Value(Arguments[0]);
	auto Uniform = Shader.GetUniform( UniformName );
	if ( !Uniform.IsValid() )
	{
		std::stringstream Error;
		Error << "Shader missing uniform " << UniformName;
		throw Soy::AssertException(Error.str());
	}

	//	get type from args
	//	gr: we dont have vector types yet, so use arrays
	auto ValueHandle = Arguments[1];
	
	if ( SoyGraphics::TElementType::IsImage(Uniform.mType) )
	{
		//	gr: we're not using the shader state, so we currently need to manually track bind count at high level
		auto BindIndexHandle = Arguments[2];
		if ( !BindIndexHandle->IsNumber() )
			throw Soy::AssertException("Currently need to pass texture bind index (increment from 0). SetUniform(Name,Image,BindIndex)");
		auto BindIndex = BindIndexHandle.As<Number>()->Int32Value();
		
		//	get the image
		auto* Image = &v8::GetObject<TImageWrapper>(ValueHandle);
		//	gr: planning ahead
		auto OnTextureLoaded = [Image,pShader,Uniform,BindIndex]()
		{
			pShader->SetUniform( Uniform, Image->GetTexture(), BindIndex );
		};
		auto OnTextureError = [](const std::string& Error)
		{
			std::Debug << "Error loading texture " << Error << std::endl;
			std::Debug << "Todo: relay to promise" << std::endl;
		};
		Image->GetTexture( OnTextureLoaded, OnTextureError );
	}
	else if ( SoyGraphics::TElementType::IsFloat(Uniform.mType) )
	{
		BufferArray<float,1024*4> Floats;
		EnumArray( ValueHandle, GetArrayBridge(Floats), Uniform.mName );
		
		//	Pad out if the uniform is an array and we're short...
		//	maybe need more strict alignment when enumerating sub arrays above
		auto UniformFloatCount = Uniform.GetFloatCount();
		if ( Floats.GetSize() < UniformFloatCount )
		{
			if ( Uniform.GetArraySize() > 1 )
			{
				for ( int i=Floats.GetSize();	i<UniformFloatCount;	i++ )
					Floats.PushBack(0);
			}
		}
		
		Shader.SetUniform( Uniform, GetArrayBridge(Floats) );
	}
	else if ( Uniform.mType == SoyGraphics::TElementType::Bool )
	{
		auto ValueBool = Local<Boolean>::Cast( ValueHandle );
		auto Bool = ValueBool->Value();
		Shader.SetUniform( Uniform, Bool );
	}
	else
	{
		throw Soy::AssertException("Currently only image & float uniform supported");
	}

	return v8::Undefined(Params.mIsolate);
}

Local<FunctionTemplate> TShaderWrapper::CreateTemplate(TV8Container& Container)
{
	auto* Isolate = Container.mIsolate;
	
	//	pass the container around
	auto ContainerHandle = External::New( Isolate, &Container );
	auto ConstructorFunc = FunctionTemplate::New( Isolate, Constructor, ContainerHandle );
	
	//	https://github.com/v8/v8/wiki/Embedder's-Guide
	//	1 field to 1 c++ object
	//	gr: we can just use the template that's made automatically and modify that!
	//	gr: prototypetemplate and instancetemplate are basically the same
	//		but for inheritance we may want to use prototype
	//		https://groups.google.com/forum/#!topic/v8-users/_i-3mgG5z-c
	auto InstanceTemplate = ConstructorFunc->InstanceTemplate();
	
	//	[0] object
	InstanceTemplate->SetInternalFieldCount(2);
	
	Container.BindFunction<SetUniform_FunctionName>( InstanceTemplate, SetUniform );

	return ConstructorFunc;
}

void TShaderWrapper::CreateShader(Opengl::TContext& Context,std::function<Opengl::TGeometry&()> GetGeo,const char* VertSource,const char* FragSource)
{
	//	this needs to be deffered along with the context..
	//	the TShader constructor needs to return a promise really
	if ( !Context.IsInitialised() )
		throw Soy::AssertException("Opengl context not yet initialised");
	
	auto& Geo = GetGeo();
	std::string VertSourceStr( VertSource );
	std::string FragSourceStr( FragSource );
	mShader.reset( new Opengl::TShader( VertSourceStr, FragSourceStr, Geo.mVertexDescription, "Shader", Context ) );

}

Opengl::TAsset OpenglObjects::GetObject(int JavascriptName,Array<std::pair<int,Opengl::TAsset>>& Buffers,std::function<void(GLuint,GLuint*)> Alloc,const char* AllocFuncName)
{
	for ( int i=0;	i<Buffers.GetSize();	i++ )
	{
		auto& Pair = Buffers[i];
		if ( Pair.first == JavascriptName )
			return Pair.second;
	}
	
	Opengl::TAsset NewBuffer;
	Alloc(1,&NewBuffer.mName);
	Opengl::IsOkay(AllocFuncName);
	if ( !NewBuffer.IsValid() )
		throw Soy::AssertException("Failed to create new buffer");
	
	Buffers.PushBack( std::make_pair(JavascriptName,NewBuffer) );
	return NewBuffer;
}


Opengl::TAsset OpenglObjects::GetBuffer(int JavascriptName)
{
	return GetObject( JavascriptName, mBuffers, glGenBuffers, "glGenBuffers" );
}

Opengl::TAsset OpenglObjects::GetFrameBuffer(int JavascriptName)
{
	return GetObject( JavascriptName, mFrameBuffers, glGenFramebuffers, "glGenFramebuffers" );
}
