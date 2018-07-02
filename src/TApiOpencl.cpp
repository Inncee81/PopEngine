#include "TApiOpencl.h"
#include "TApiCommon.h"
#include <SoyGraphics.h>

using namespace v8;

const char OpenclEnumDevices_FunctionName[] = "OpenclEnumDevices";
const char ExecuteKernel_FunctionName[] = "ExecuteKernel";
const char SetUniform_FunctionName[] = "SetUniform";
const char ReadUniform_FunctionName[] = "ReadUniform";


static v8::Local<v8::Value> OpenclEnumDevices(v8::CallbackInfo& Params);


void ApiOpencl::Bind(TV8Container& Container)
{
	Container.BindObjectType("OpenclContext", TOpenclContext::CreateTemplate );
	Container.BindObjectType("OpenclKernel", TOpenclKernel::CreateTemplate );
	Container.BindObjectType( TOpenclKernelState::GetObjectTypeName(), TOpenclKernelState::CreateTemplate );
	Container.BindGlobalFunction<OpenclEnumDevices_FunctionName>( OpenclEnumDevices );
}



class TOpenclRunner : public PopWorker::TJob
{
public:
	TOpenclRunner(Opencl::TContext& Context,Opencl::TKernel& Kernel) :
	mContext	( Context ),
	mKernel		( Kernel )
	{
	}
	
	virtual void		Run() override;
	
protected:
	//	get iterations and can setup first set of kernel args
	//	number of elements in the array dictates dimensions
	virtual void		Init(Opencl::TKernelState& Kernel,ArrayBridge<vec2x<size_t>>&& Iterations)=0;
	
	//	set any iteration-specific args
	virtual void		RunIteration(Opencl::TKernelState& Kernel,const Opencl::TKernelIteration& WorkGroups,bool& Block)=0;
	
	//	after last iteration - read back data etc
	virtual void		OnFinished(Opencl::TKernelState& Kernel)=0;
	
	virtual void		OnError(std::exception& Error)=0;
	
public:
	Opencl::TKernel&	mKernel;
	Opencl::TContext&	mContext;
};


class TOpenclRunnerLambda : public TOpenclRunner
{
public:
	TOpenclRunnerLambda(Opencl::TContext& Context,Opencl::TKernel& Kernel,std::function<void(Opencl::TKernelState&,ArrayBridge<vec2x<size_t>>&)> InitLambda,std::function<void(Opencl::TKernelState&,const Opencl::TKernelIteration&,bool&)> IterationLambda,std::function<void(Opencl::TKernelState&)> FinishedLambda,std::function<void(std::exception&)> ErrorLambda) :
		TOpenclRunner		( Context, Kernel ),
		mIterationLambda	( IterationLambda ),
		mInitLambda			( InitLambda ),
		mFinishedLambda		( FinishedLambda ),
		mErrorLambda		( ErrorLambda )
	{
	}
	
	virtual void		Init(Opencl::TKernelState& Kernel,ArrayBridge<vec2x<size_t>>&& Iterations) override
	{
		mInitLambda( Kernel, Iterations );
	}
	
	virtual void		RunIteration(Opencl::TKernelState& Kernel,const Opencl::TKernelIteration& WorkGroups,bool& Block) override
	{
		mIterationLambda( Kernel, WorkGroups, Block );
	}
	
	//	after last iteration - read back data etc
	virtual void		OnFinished(Opencl::TKernelState& Kernel) override
	{
		mFinishedLambda( Kernel );
	}
	
	virtual void		OnError(std::exception& Error) override
	{
		mErrorLambda( Error );
	}

public:
	std::function<void(Opencl::TKernelState&,ArrayBridge<vec2x<size_t>>&)>	mInitLambda;
	std::function<void(Opencl::TKernelState&,const Opencl::TKernelIteration&,bool&)>	mIterationLambda;
	std::function<void(Opencl::TKernelState&)>						mFinishedLambda;
	std::function<void(std::exception&)>							mErrorLambda;
};


void TOpenclRunner::Run()
{
	//ofScopeTimerWarning Timer( (std::string("Opencl ") + this->mKernel.mKernelName).c_str(), 0 );
	auto Kernel = mKernel.Lock(mContext);
	
	//	get iterations we want
	Array<vec2x<size_t>> Iterations;
	Init( Kernel, GetArrayBridge( Iterations ) );
	
	//	divide up the iterations
	Array<Opencl::TKernelIteration> IterationSplits;
	Kernel.GetIterations( GetArrayBridge(IterationSplits), GetArrayBridge(Iterations) );
	
	//	for now, because buffers get realeased etc when the kernelstate is destructed,
	//	lets just block on the last execution to make sure nothing is in use. Optimise later.
	Opencl::TSync LastSemaphore;
	static bool BlockLast = true;
	
	try
	{
		for ( int i=0;	i<IterationSplits.GetSize();	i++ )
		{
			auto& Iteration = IterationSplits[i];
			
			//	setup the iteration
			bool Block = false;
			RunIteration( Kernel, Iteration, Block );
			
			//	execute it
			Opencl::TSync ItSemaphore;
			auto* Semaphore = Block ? &ItSemaphore : nullptr;
			if ( BlockLast && i == IterationSplits.GetSize()-1 )
				Semaphore = &LastSemaphore;
			
			if ( Semaphore )
			{
				Kernel.QueueIteration( Iteration, *Semaphore );
				Semaphore->Wait();
			}
			else
			{
				Kernel.QueueIteration( Iteration );
			}
		}
		
		LastSemaphore.Wait();
		
		OnFinished( Kernel );
	}
	catch(std::exception& e)
	{
		OnError( e );
	}
}



static Local<Value> OpenclEnumDevices(CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;

	//	get devices
	Array<Opencl::TDeviceMeta> Devices;
	auto Filter = OpenclDevice::Type::GPU;
	Opencl::GetDevices( GetArrayBridge(Devices), Filter );
	
	//	make an array to return
	//	todo: use v8::GetArray
	auto DevicesArray = v8::Array::New( &Params.GetIsolate() );
	for ( auto i=0;	i<Devices.GetSize();	i++ )
	{
		auto& Device = Devices[i];
		auto DeviceName = v8::GetString( Params.GetIsolate(), Device.mName );
		DevicesArray->Set( i, DeviceName );
	}
	
	return DevicesArray;
}



TOpenclContext::TOpenclContext(TV8Container& Container,const std::string& DeviceName) :
	mContainer	( Container )
{
	//	find the device
	Opengl::TContext* OpenglContext = nullptr;
	
	Array<Opencl::TDeviceMeta> DeviceMetas;
	auto EnumDevice = [&](const Opencl::TDeviceMeta& Device)
	{
		auto MatchDeviceName = Device.mName;
		if ( !Soy::StringContains( MatchDeviceName, DeviceName, false ) )
			return;
		DeviceMetas.PushBack( Device );
	};
	Opencl::EnumDevices( EnumDevice );
	
	mOpenclDevice.reset( new Opencl::TDevice( GetArrayBridge(DeviceMetas) ) );
	mOpenclContext = mOpenclDevice->CreateContextThread( DeviceName );
	
}


Local<FunctionTemplate> TOpenclContext::CreateTemplate(TV8Container& Container)
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
	
	Container.BindFunction<ExecuteKernel_FunctionName>( InstanceTemplate, ExecuteKernel );

	return ConstructorFunc;
}


void TOpenclContext::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	if ( Arguments.Length() != 1 )
	{
		auto Exception = Isolate->ThrowException( v8::GetString( *Arguments.GetIsolate(), "Expected 1 argument, OpenclContext(DeviceName)"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	auto This = Arguments.This();
	auto& Container = v8::GetObject<TV8Container>( Arguments.Data() );
	
	//	get the device
	auto DeviceName = v8::GetString( Arguments[0] );
	
	//	alloc window
	//	gr: this should be OWNED by the context (so we can destroy all c++ objects with the context)
	//		but it also needs to know of the V8container to run stuff
	//		cyclic hell!
	auto* NewContext = new TOpenclContext( Container, DeviceName );
	
	NewContext->mHandle.Reset( Isolate, Arguments.This() );
	
	//	set fields
	This->SetInternalField( 0, External::New( Arguments.GetIsolate(), NewContext ) );

	// return the new object back to the javascript caller
	Arguments.GetReturnValue().Set( This );
}









TOpenclKernel::TOpenclKernel(TV8Container& Container,Opencl::TContext& Context,const std::string& Source,const std::string& KernelName) :
	mContainer	( Container )
{
	mProgram.reset( new Opencl::TProgram( Source, Context ) );
	auto& Program = *mProgram;
	mKernel.reset( new Opencl::TKernel( KernelName, Program ) );
}


Local<FunctionTemplate> TOpenclKernel::CreateTemplate(TV8Container& Container)
{
	auto* Isolate = Container.mIsolate;
	
	//	pass the container around
	auto ContainerHandle = External::New( Isolate, &Container );
	auto ConstructorFunc = FunctionTemplate::New( Isolate, Constructor, ContainerHandle );
	
	auto InstanceTemplate = ConstructorFunc->InstanceTemplate();
	//	[0] object
	//	[1] container
	InstanceTemplate->SetInternalFieldCount(2);
	
	return ConstructorFunc;
}


void TOpenclKernel::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	if ( Arguments.Length() != 3 )
	{
		auto Exception = Isolate->ThrowException( v8::GetString( *Arguments.GetIsolate(), "Expected 2 arguments, OpenclKernel(Context,Source,KernelName)"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	auto This = Arguments.This();
	auto& Container = v8::GetObject<TV8Container>( Arguments.Data() );
	auto& Context = v8::GetObject<TOpenclContext>( Arguments[0] );
	auto KernelSource = v8::GetString( Arguments[1] );
	auto KernelName = v8::GetString( Arguments[2] );

	//	alloc
	auto& ContextContext = *Context.mOpenclContext;
	auto* NewKernel = new TOpenclKernel( Container, ContextContext, KernelSource, KernelName );
	
	NewKernel->mHandle.Reset( Isolate, This );
	
	//	set fields
	This->SetInternalField( 0, External::New( Arguments.GetIsolate(), NewKernel ) );
	
	// return the new object back to the javascript caller
	Arguments.GetReturnValue().Set( This );
}


v8::Local<v8::Value> TOpenclContext::ExecuteKernel(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	auto& This = v8::GetObject<TOpenclContext>( Arguments.This() );
	auto* Isolate = Params.mIsolate;
	auto* Container = &Params.mContainer;

	//	make a promise resolver (persistent to copy to thread)
	auto Resolver = v8::Promise::Resolver::New( Isolate );
	auto ResolverPersistent = v8::GetPersistent( *Isolate, Resolver );
	
	//auto KernelPersistent = v8::GetPersistent( *Isolate, Arguments[0] );
	auto& Kernel = v8::GetObject<TOpenclKernel>(Arguments[0]);
	BufferArray<int,3> IterationCount;
	v8::EnumArray( Arguments[1], GetArrayBridge(IterationCount) );
	auto IterationCallbackPersistent = v8::GetPersistent( *Isolate, Local<Function>::Cast(Arguments[2]) );
	auto FinishedCallbackPersistent = v8::GetPersistent( *Isolate, Local<Function>::Cast(Arguments[3]) );
	
	This.DoExecuteKernel( Kernel, IterationCount, IterationCallbackPersistent, FinishedCallbackPersistent, ResolverPersistent );
 
	
	//	return the promise
	auto Promise = Resolver->GetPromise();
	return Promise;
}


void TOpenclContext::DoExecuteKernel(TOpenclKernel& Kernel,BufferArray<int,3> IterationCount,Persist<Function> IterationCallback,Persist<Function> FinishedCallback,Persist<Promise::Resolver> Resolver)
{
	auto* Isolate = &this->mContainer.GetIsolate();
	auto* Container = &this->mContainer;
	/*
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
	 */
	auto OnCompleted = [=](Local<Context> Context)
	{
		auto ResolverLocal = v8::GetLocal( *Isolate, Resolver );
		ResolverLocal->Resolve( v8::Undefined( Isolate ) );
	};
	
	auto KernelInit = [IterationCount](Opencl::TKernelState&,ArrayBridge<vec2x<size_t>>& IterationMeta)
	{
		//	the original implementation is a min/max
		//	now that iterations are at a high level, this information is a bit superfolous
		for ( int i=0;	i<IterationCount.GetSize();	i++ )
		{
			auto DimensionMin = 0;
			auto DimensionMax = IterationCount[i];
			IterationMeta.PushBack( vec2x<size_t>( DimensionMin, DimensionMax ) );
		}
	};
	
	auto KernelIteration = [=](Opencl::TKernelState& KernelState,const Opencl::TKernelIteration& Iteration,bool&)
	{
		auto ExecuteIteration = [&](Local<Context> Context)
		{
			//	create temp reference to the kernel state
			auto KernelStateHandle = Container->CreateObjectInstance<TOpenclKernelState>( KernelState);
			BufferArray<int,3> IterationIndexes;
			auto IterationIndexesHandle = v8::GetArray( *Isolate, GetArrayBridge(Iteration.mFirst) );
			BufferArray<Local<Value>,10> CallbackParams;
			CallbackParams.PushBack( KernelStateHandle );
			CallbackParams.PushBack( IterationIndexesHandle );
			Container->ExecuteFunc( Context, IterationCallback, GetArrayBridge(CallbackParams) );
		};
		Container->RunScoped( ExecuteIteration );
	};
	
	auto KernelFinished = [=](Opencl::TKernelState& KernelState)
	{
		auto ExecuteFinished = [&](Local<Context> Context)
		{
			auto KernelStateHandle = Container->CreateObjectInstance<TOpenclKernelState>( KernelState);
			BufferArray<Local<Value>,10> CallbackParams;
			CallbackParams.PushBack( KernelStateHandle );
			Container->ExecuteFunc( Context, FinishedCallback, GetArrayBridge(CallbackParams) );
		};
		Container->RunScoped( ExecuteFinished );
		Container->QueueScoped( OnCompleted );
	};
	
	auto KernelError = [=](std::exception& Exception)
	{
		std::string Error;
		Error += "Kernel Error: ";
		Error += Exception.what();
		
		auto OnError = [=](Local<Context> Context)
		{
			auto ResolverLocal = v8::GetLocal( *Isolate, Resolver );
			auto ErrorStr = v8::GetString( *Isolate, Error );
			ResolverLocal->Reject( ErrorStr );
		};
		Container->QueueScoped( OnError );
	};

	auto OpenclRun = [=]
	{
		/*
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
		 RenderTarget.SetViewportNormalised( Soy::Rectf(0,0,1,1) );
		 try
		 {
		 //	immediately call the javascript callback
		 Container->RunScoped( ExecuteRenderCallback );
		 RenderTarget.Unbind();
		 }
		 catch(std::exception& e)
		 {
		 RenderTarget.Unbind();
		 throw;
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
		 */
	};
	
	auto& OpenclContext = *mOpenclContext;
	auto* JobRunner = new TOpenclRunnerLambda( OpenclContext, Kernel.GetKernel(), KernelInit, KernelIteration, KernelFinished, KernelError );
	std::shared_ptr<PopWorker::TJob> Job(JobRunner);
	OpenclContext.PushJob( Job );
}







Local<FunctionTemplate> TOpenclKernelState::CreateTemplate(TV8Container& Container)
{
	auto* Isolate = Container.mIsolate;
	
	//	pass the container around
	auto ContainerHandle = External::New( Isolate, &Container );
	auto ConstructorFunc = FunctionTemplate::New( Isolate, Constructor, ContainerHandle );
	
	auto InstanceTemplate = ConstructorFunc->InstanceTemplate();
	//	[0] object
	//	[1] container
	InstanceTemplate->SetInternalFieldCount(2);
	Container.BindFunction<SetUniform_FunctionName>( InstanceTemplate, SetUniform );
	Container.BindFunction<ReadUniform_FunctionName>( InstanceTemplate, ReadUniform );

	return ConstructorFunc;
}


void TOpenclKernelState::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Not allowed to manually construct TOpenclKernelState"));
	Arguments.GetReturnValue().Set(Exception);
	return;
}

std::shared_ptr<Opencl::TBuffer> GetFloat4BufferArray(Local<Value> ValueHandle,Opencl::TContext& Context,const std::string& Name)
{
	Array<float> Floats;
	EnumArray( ValueHandle, GetArrayBridge(Floats) );
	if ( (Floats.GetSize() % 4)!=0 )
	{
		std::stringstream Error;
		Error << Name << ": Number of floats(" << Floats.GetSize() << ") doesn't align to 4";
		throw Soy::AssertException(Error.str());
	}
	
	Array<cl_float4> Float4s;
	for ( int i=0;	i<Floats.GetSize();	i+=4 )
	{
		auto& f4 = Float4s.PushBack();
		f4.s[0] = Floats[i+0];
		f4.s[1] = Floats[i+1];
		f4.s[2] = Floats[i+2];
		f4.s[3] = Floats[i+3];
	}
	
	auto Buffer = Opencl::TBufferArray<cl_float4>::Alloc( GetArrayBridge(Float4s), Context, Name );
	return Buffer;
}


std::shared_ptr<Opencl::TBuffer> GetIntBufferArray(Local<Value> ValueHandle,Opencl::TContext& Context,const std::string& Name)
{
	Array<int> Ints;
	EnumArray( ValueHandle, GetArrayBridge(Ints) );
	
	Array<cl_int> IntCls;
	for ( int i=0;	i<Ints.GetSize();	i++ )
	{
		IntCls.PushBack( Ints[i] );
	}
	
	auto Buffer = Opencl::TBufferArray<cl_int>::Alloc( GetArrayBridge(IntCls), Context, Name );
	return Buffer;
}

v8::Local<v8::Value> TOpenclKernelState::SetUniform(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	//	gr: being different to all the others...
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& KernelState = v8::GetObject<Opencl::TKernelState>( ThisHandle );
	
	auto UniformName = v8::GetString(Arguments[0]);
	auto Uniform = KernelState.GetUniform( UniformName );
	
	//	get type from args
	//	gr: we dont have js vector types yet, so use arrays
	auto ValueHandle = Arguments[1];
	
	if ( Uniform.mType == "int" )
	{
		BufferArray<int,1> Ints;
		EnumArray( ValueHandle, GetArrayBridge(Ints) );
		KernelState.SetUniform( UniformName.c_str(), Ints[0] );
	}
	else if ( Uniform.mType == "float4*" )
	{
		//	need to check here for buffer reuse
		auto& Context = KernelState.GetContext();
		auto BufferArray = GetFloat4BufferArray( ValueHandle, Context, Uniform.mName );
		KernelState.SetUniform( UniformName, BufferArray );
	}
	else if ( Uniform.mType == "int*" )
	{
		//	need to check here for buffer reuse
		auto& Context = KernelState.GetContext();
		auto BufferArray = GetIntBufferArray( ValueHandle, Context, Uniform.mName );
		KernelState.SetUniform( UniformName, BufferArray );
	}
	else
	{
		std::stringstream Error;
		Error << "Unhandled uniform type [" << Uniform.mType << "] for " << Uniform.mName;
		throw Soy::AssertException( Error.str() );
	}
	/*
	if ( SoyGraphics::TElementType::IsImage(Uniform.mType) )
	{
		//	gr: we're not using the shader state, so we currently need to manually track bind count at high level
		auto BindIndexHandle = Arguments[2];
		if ( !BindIndexHandle->IsNumber() )
			throw Soy::AssertException("Currently need to pass texture bind index (increment from 0). SetUniform(Name,Image,BindIndex)");
		auto BindIndex = BindIndexHandle.As<Number>()->Int32Value();
		
		//	get the image
		auto& Image = v8::GetObject<TImageWrapper>(ValueHandle);
		//	gr: planning ahead
		auto OnTextureLoaded = [&Image,pShader,Uniform,BindIndex]()
		{
			pShader->SetUniform( Uniform, Image.GetTexture(), BindIndex );
		};
		auto OnTextureError = [](const std::string& Error)
		{
			std::Debug << "Error loading texture " << Error << std::endl;
			std::Debug << "Todo: relay to promise" << std::endl;
		};
		Image.GetTexture( OnTextureLoaded, OnTextureError );
	}
	else if ( SoyGraphics::TElementType::IsFloat(Uniform.mType) )
	{
		BufferArray<float,1024*4> Floats;
		EnumArray( ValueHandle, GetArrayBridge(Floats) );
		
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
	else
	{
		throw Soy::AssertException("Currently only image & float uniform supported");
	}
	*/
	return v8::Undefined(Params.mIsolate);
}




v8::Local<v8::Value> TOpenclKernelState::ReadUniform(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	//	gr: being different to all the others...
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& KernelState = v8::GetObject<Opencl::TKernelState>( ThisHandle );
	
	auto UniformName = v8::GetString(Arguments[0]);
	auto Uniform = KernelState.GetUniform( UniformName );

	//	work out what to do from type
	if ( Uniform.mType == "float4*" )
	{
		Array<cl_float4> Values;
		KernelState.ReadUniform( Uniform.mName.c_str(), GetArrayBridge(Values) );
		auto Valuesf = GetRemoteArray( reinterpret_cast<float*>(Values.GetArray()), Values.GetSize()*4 );
		auto ValuesArray = v8::GetArray( Params.GetIsolate(), GetArrayBridge(Valuesf) );
		return ValuesArray;
	}
	else if ( Uniform.mType == "int*" )
	{
		Array<int> Values;
		KernelState.ReadUniform( Uniform.mName.c_str(), GetArrayBridge(Values) );
		auto ValuesArray = v8::GetArray( Params.GetIsolate(), GetArrayBridge(Values) );
		return ValuesArray;
	}

	std::stringstream Error;
	Error << "Unhandled uniform type [" << Uniform.mType << "] for " << Uniform.mName;
	throw Soy::AssertException( Error.str() );
}


