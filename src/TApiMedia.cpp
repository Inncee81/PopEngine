#include "TApiMedia.h"
#include "SoyOpenglWindow.h"
#include "TApiCommon.h"
#include "SoyFilesystem.h"

#include "PopMovie/AvfVideoCapture.h"
#include "PopMovie/AvfMovieDecoder.h"
#include "SoyDecklink/SoyDecklink.h"

#include "Libs/PopH264Framework.framework/Headers/PopH264DecoderInstance.h"

using namespace v8;

namespace ApiMedia
{
	const char Namespace[] = "Pop.Media";
	
	v8::Local<v8::Value>	EnumDevices(const v8::CallbackInfo& Params);
}

const char EnumDevices_FunctionName[] = "EnumDevices";

const char MediaSource_TypeName[] = "Source";
const char Free_FunctionName[] = "Free";
const char GetNextFrame_FunctionName[] = "GetNextFrame";

const char AvcDecoder_TypeName[] = "AvcDecoder";
const char Decode_FunctionName[] = "decode";

const char FrameTimestampKey[] = "Time";

void ApiMedia::Bind(TV8Container& Container)
{
	Container.CreateGlobalObjectInstance("", Namespace);

	Container.BindGlobalFunction<EnumDevices_FunctionName>( ApiMedia::EnumDevices, Namespace );

	Container.BindObjectType( TMediaSourceWrapper::GetObjectTypeName(), TMediaSourceWrapper::CreateTemplate, TMediaSourceWrapper::Allocate<TMediaSourceWrapper>, Namespace );
	Container.BindObjectType( TAvcDecoderWrapper::GetObjectTypeName(), TAvcDecoderWrapper::CreateTemplate, TAvcDecoderWrapper::Allocate<TAvcDecoderWrapper>, Namespace );
}



v8::Local<v8::Value> ApiMedia::EnumDevices(const v8::CallbackInfo& Params)
{
	auto* Isolate = Params.mIsolate;
	
	//	make a promise resolver (persistent to copy to thread)
	auto Resolver = v8::Promise::Resolver::New( Isolate );
	auto ResolverPersistent = v8::GetPersistent( Params.GetIsolate(), Resolver );

	auto* Container = &Params.mContainer;
	
	auto DoEnumDevices = [=]
	{
		try
		{
			Array<std::string> DeviceNames;
			auto EnumDevice = [&](const std::string& Name)
			{
				DeviceNames.PushBack(Name);
			};
			
			try
			{
				::Platform::EnumCaptureDevices(EnumDevice);
			}
			catch(std::exception& e)
			{
				std::Debug << e.what() << std::endl;
			}
			
			try
			{
				Decklink::EnumDevices(EnumDevice);
			}
			catch(std::exception& e)
			{
				std::Debug << e.what() << std::endl;
			}
			
			auto OnCompleted = [=](Local<Context> Context)
			{
				//	return face points here
				//	gr: can't do this unless we're in the javascript thread...
				auto ResolverLocal = ResolverPersistent->GetLocal(*Isolate);
				//ResolverPersistent.Reset();
				auto GetValue = [&](size_t Index)
				{
					return v8::GetString(*Context->GetIsolate(), DeviceNames[Index] );
				};
				auto DeviceNamesArray = v8::GetArray( *Context->GetIsolate(), DeviceNames.GetSize(), GetValue );
				ResolverLocal->Resolve( DeviceNamesArray );
			};
			
			//	queue the completion, doesn't need to be done instantly
			Container->QueueScoped( OnCompleted );
		}
		catch(std::exception& e)
		{
			std::Debug << e.what() << std::endl;
			
			//	queue the error callback
			std::string ExceptionString(e.what());
			auto OnError = [=](Local<Context> Context)
			{
				auto ResolverLocal = ResolverPersistent->GetLocal(*Isolate);
				//ResolverPersistent.Reset();
				auto Error = String::NewFromUtf8( Isolate, ExceptionString.c_str() );
				ResolverLocal->Reject( Error );
			};
			Container->QueueScoped( OnError );
		}
	};
	
	//	immediate... if this is slow, put it on a thread
	DoEnumDevices();
	
	//	return the promise
	auto Promise = Resolver->GetPromise();
	return Promise;
}


TMediaSourceWrapper::~TMediaSourceWrapper()
{
	std::Debug << __func__ << std::endl;
	if ( mExtractor )
	{
		std::Debug << __func__ << " stopping extractor" << std::endl;
		mExtractor->Stop();
		mExtractor.reset();
	}
	std::Debug << __func__ << " finished" << std::endl;
}

std::shared_ptr<TMediaExtractor> TMediaSourceWrapper::AllocExtractor(const TMediaExtractorParams& Params)
{
	//	video extractor if it's a filename
	if ( ::Platform::FileExists(Params.mFilename) )
	{
		std::shared_ptr<Opengl::TContext> OpenglContext;
		
		auto Extractor = ::Platform::AllocVideoDecoder( Params, OpenglContext );
		if ( Extractor )
			return Extractor;
	}
	
	//	try decklink devices
	{
		try
		{
			auto Extractor = Decklink::AllocExtractor(Params);
			if ( Extractor )
				return Extractor;
		}
		catch(std::exception& e)
		{
			std::Debug << e.what() << std::endl;
		}
	}
	
	//	try platforms capture devices
	{
		auto Extractor = ::Platform::AllocCaptureExtractor( Params, nullptr );
		if ( Extractor )
			return Extractor;
	}
	
	std::stringstream Error;
	Error << "Failed to allocate a device matching " << Params.mFilename;
	throw Soy::AssertException(Error.str());
}


void TMediaSourceWrapper::Construct(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;

	using namespace v8;
	auto* Isolate = Arguments.GetIsolate();
	
	auto DeviceNameHandle = Arguments[0];
	auto SinglePlaneOutputHandle = Arguments[1];
	auto FilterCallbackHandle = Arguments[2];
	auto MaxBufferSizeHandle = Arguments[3];
	
	size_t MaxBufferSize = 10;
	if ( !MaxBufferSizeHandle->IsUndefined() )
		MaxBufferSize = v8::SafeCast<v8::Number>(MaxBufferSizeHandle)->Int32Value();
	
	bool SinglePlaneOutput = false;
	if ( !SinglePlaneOutputHandle->IsUndefined() )
		SinglePlaneOutput = v8::SafeCast<v8::Boolean>(SinglePlaneOutputHandle)->BooleanValue();
		
	if ( !FilterCallbackHandle->IsUndefined() )
	{
		auto FilterCallback = v8::SafeCast<v8::Function>(FilterCallbackHandle);
		mOnFrameFilter = v8::GetPersistent( *Isolate, FilterCallback );
	}
	
	auto OnFrameExtracted = [=](const SoyTime Time,size_t StreamIndex)
	{
		//std::Debug << "Got stream[" << StreamIndex << "] frame at " << Time << std::endl;
		this->OnNewFrame(StreamIndex);
	};
	auto OnPrePushFrame = [](TPixelBuffer&,const TMediaExtractorParams&)
	{
		//	gr: do filter here!
		//std::Debug << "OnPrePushFrame" << std::endl;
	};

	//	create device
	auto DeviceName = v8::GetString( DeviceNameHandle );
	TMediaExtractorParams ExtractorParams( DeviceName, DeviceName, OnFrameExtracted, OnPrePushFrame );
	ExtractorParams.mForceNonPlanarOutput = SinglePlaneOutput;
	ExtractorParams.mDiscardOldFrames = false;
	
	mExtractor = AllocExtractor(ExtractorParams);
	mExtractor->AllocStreamBuffer(0,MaxBufferSize);
	mExtractor->Start(false);
}


Local<FunctionTemplate> TMediaSourceWrapper::CreateTemplate(TV8Container& Container)
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
	Container.BindFunction<Free_FunctionName>( InstanceTemplate, Free );
	Container.BindFunction<GetNextFrame_FunctionName>( InstanceTemplate, GetNextFrame );

	return ConstructorFunc;
}


void TMediaSourceWrapper::OnNewFrame(size_t StreamIndex)
{
	//	do filter here
	if ( mOnFrameFilter )
	{
		//	When we're stuck with non-async stuff in js (posenet)
		//	lets do an immediate "reject frame" option
		//	and if the isolate is yeilded (sleep()) then this can execute now
		bool AllowFrame = true;
		auto FilterRunner = [this,&AllowFrame](Local<Context> context)
		{
			auto& Isolate = *context->GetIsolate();
			
			BufferArray<Local<Value>,2> Args;
			
			auto FuncHandle = mOnFrameFilter->GetLocal(Isolate);
			auto ThisHandle = v8::Local<v8::Object>();
			
			auto AllowHandle = mContainer.ExecuteFunc( context, FuncHandle, ThisHandle, GetArrayBridge(Args) );
			auto AllowBoolHandle = v8::SafeCast<v8::Boolean>(AllowHandle);
			AllowFrame = AllowBoolHandle->BooleanValue();
		};
		mContainer.RunScoped( FilterRunner );
		
		if ( !AllowFrame )
		{
			//	discard the frame by popping it
			auto PacketBuffer = this->mExtractor->GetStreamBuffer(StreamIndex);
			auto FramePacket = PacketBuffer->PopPacket();
			FramePacket.reset();
			return;
		}
	}
	
	//	notify that there's a new frame
	auto Runner = [this](Local<Context> context)
	{
		auto* isolate = context->GetIsolate();
		auto This = this->mHandle.Get(isolate);
		
		BufferArray<Local<Value>,2> Args;
		
		auto FuncHandle = v8::GetFunction( context, This, "OnNewFrame" );
	
		try
		{
			mContainer.ExecuteFunc( context, FuncHandle, This, GetArrayBridge(Args) );
		}
		catch(std::exception& e)
		{
			std::Debug << "OnNewFrame Exception: " << e.what() << std::endl;
		}
	};
	mContainer.QueueScoped( Runner );
}



v8::Local<v8::Value> TMediaSourceWrapper::GetNextFrame(const v8::CallbackInfo& Params)
{
	auto& This = Params.GetThis<TMediaSourceWrapper>();

	//	grab frame
	auto StreamIndex = 0;
	auto PacketBuffer = This.mExtractor->GetStreamBuffer(StreamIndex);
	auto FramePacket = PacketBuffer->PopPacket();
	if ( !FramePacket )
		throw Soy::AssertException("No frame packet buffered");
	auto PixelBuffer = FramePacket->mPixelBuffer;
	if ( PixelBuffer == nullptr )
		throw Soy::AssertException("Missing Pixel buffer in frame");
	
	//	convert to pixels here (for testing if image pixel buffer is/n't working)
	static bool ReadToPixels = false;
	std::shared_ptr<SoyPixels> Pixels;

	if ( ReadToPixels )
	{
		//	may get 2 planes
		BufferArray<SoyPixelsImpl*,2> Textures;
		float3x3 Transform;
		PixelBuffer->Lock( GetArrayBridge(Textures), Transform );
		try
		{
			auto& Heap = Params.mContainer.GetImageHeap();
			Pixels.reset( new SoyPixels(Heap) );
			auto& RgbPixels = *Pixels;
			RgbPixels.Copy( *Textures[0] );
			PixelBuffer->Unlock();
		}
		catch(std::exception& e)
		{
			PixelBuffer->Unlock();
			throw;
		}
	}

	
	auto* pImage = new TImageWrapper( Params.mContainer );
	pImage->mName = "MediaSource Frame";
	auto& Image = *pImage;
	if ( Pixels )
		Image.SetPixels(Pixels);
	else
		Image.SetPixelBuffer(PixelBuffer);

	auto ImageHandle = Image.GetHandle();
	auto FrameTime = FramePacket->GetStartTime();
	if ( FrameTime.IsValid() )
	{
		auto FrameTimeDouble = FrameTime.mTime;
		auto FrameTimeKey = v8::GetString( Params.GetIsolate(), FrameTimestampKey );
		auto FrameTimeValue = v8::Number::New( &Params.GetIsolate(), FrameTimeDouble );
		ImageHandle->Set( FrameTimeKey, FrameTimeValue );
	}
	
	return ImageHandle;
}


v8::Local<v8::Value> TMediaSourceWrapper::Free(const v8::CallbackInfo& Params)
{
	auto& This = Params.GetThis<TMediaSourceWrapper>();
	This.mExtractor.reset();
	
	return v8::Undefined(Params.mIsolate);
}


void TAvcDecoderWrapper::Construct(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	using namespace v8;
	auto* Isolate = Arguments.GetIsolate();
	
	mDecoder.reset( new TDecoderInstance );
}

Local<FunctionTemplate> TAvcDecoderWrapper::CreateTemplate(TV8Container& Container)
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
	Container.BindFunction<Decode_FunctionName>( InstanceTemplate, Decode );
	
	return ConstructorFunc;
}

v8::Local<v8::Value> TAvcDecoderWrapper::Decode(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto PacketBytesHandle = Arguments[0];
	auto DecoderMeta = Arguments[1];
	auto DecodeCallbackHandle = Arguments[2];
	auto& This = Params.GetThis<TAvcDecoderWrapper>();
	
	//	check if undefined, if it's defined and not a func, we'll throw so user knows they've made a mistake;
	auto DecodeCallbackHandleIsValid = !DecodeCallbackHandle->IsUndefined();
	
	//	get array
	Array<uint8_t> PacketBytes;
	v8::EnumArray<v8::Uint8Array>( PacketBytesHandle, GetArrayBridge(PacketBytes) );
	
	auto OnImage = [&](const SoyPixelsImpl& Frame)
	{
		//	create an image
		This.OnNewFrame( Frame );
		
		//	return each plane as an image arg (maybe return an array?)
		BufferArray<Local<Value>,4> Args;
		
		Array<std::shared_ptr<SoyPixelsImpl>> FramePlanes;
		Frame.SplitPlanes( GetArrayBridge(FramePlanes) );
		for ( auto p=0;	p<FramePlanes.GetSize();	p++)
		{
			auto& PlanePixels = *FramePlanes[p];
			//	gr: this looks leaky, but its not, its refcounted by JS
			auto* pImage = new TImageWrapper( Params.mContainer );
			pImage->mName = "MediaSource Frame";
			auto& Image = *pImage;
			Image.SetPixels(PlanePixels);
			auto ImageHandle = Image.GetHandle();
			Args.PushBack(ImageHandle);
		}
		
		auto DecodeCallbackHandleFunc = v8::Local<Function>::Cast( DecodeCallbackHandle );
		auto DecodeCallbackThis = Params.mContext->Global();
		Params.mContainer.ExecuteFunc( Params.mContext, DecodeCallbackHandleFunc, DecodeCallbackThis, GetArrayBridge(Args) );
	};
	
	//	send a null callback to skip the picture extraction
	std::function<void(const SoyPixelsImpl&)> OnImageDecoded;
	if ( DecodeCallbackHandleIsValid )
		OnImageDecoded = OnImage;
	
	//	this function is synchronous, so it should put stuff straight back in the queue
	//	the callback was handy though, so maybe go back to it
	This.mDecoder->PushData( PacketBytes.GetArray(), PacketBytes.GetDataSize(), 0 );

	
	
	TFrame Frame;
	while ( This.mDecoder->PopFrame(Frame) )
	{
		auto& Pixels = *Frame.mPixels;
		OnImageDecoded( Pixels );
	}

	return v8::Undefined(Params.mIsolate);
}


void TAvcDecoderWrapper::OnNewFrame(const SoyPixelsImpl& Pixels)
{
	//	onPictureDecoded to match braodway WASM API
	
	//	notify that there's a new frame
	auto Runner = [this](Local<Context> context)
	{
		auto* isolate = context->GetIsolate();
		auto This = this->mHandle.Get(isolate);
		
		BufferArray<Local<Value>,1> Args;
		
		try
		{
			auto FuncHandle = v8::GetFunction( context, This, "onPictureDecoded" );
			mContainer.ExecuteFunc( context, FuncHandle, This, GetArrayBridge(Args) );
		}
		catch(std::exception& e)
		{
			std::Debug << "onPictureDecoded Exception: " << e.what() << std::endl;
		}
	};
	mContainer.QueueScoped( Runner );
}
