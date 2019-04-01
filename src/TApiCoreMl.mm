#include "TApiCoreMl.h"
#include "TApiCommon.h"
#include "SoyAvf.h"
#include "SoyLib/src/SoyScope.h"

//#import "MobileNet.h"
#import "TinyYOLO.h"
#import "Hourglass.h"
#import "Cpm.h"

//	openpose model from here
//	https://github.com/infocom-tpo/SwiftOpenPose
#import "MobileOpenPose.h"

//	ssd+mobilenet from here (just want ssd!)
//	https://github.com/vonholst/SSDMobileNet_CoreML/blob/master/SSDMobileNet/SSDMobileNet/ssd_mobilenet_feature_extractor.mlmodel
#import "SsdMobilenet.h"
#include "TSsdMobileNetAnchors.h"

//	https://github.com/edouardlp/Mask-RCNN-CoreML
#import "MaskRCNN_MaskRCNN.h"

#import <Vision/Vision.h>

const char Yolo_FunctionName[] = "Yolo";
const char Hourglass_FunctionName[] = "Hourglass";
const char Cpm_FunctionName[] = "Cpm";
const char OpenPose_FunctionName[] = "OpenPose";
const char SsdMobileNet_FunctionName[] = "SsdMobileNet";
const char MaskRcnn_FunctionName[] = "MaskRcnn";
const char FaceDetect_FunctionName[] = "FaceDetect";

const char CoreMl_TypeName[] = "CoreMl";


void ApiCoreMl::Bind(Bind::TContext& Context)
{
	Context.BindObjectType<TCoreMlWrapper>( ApiPop::Namespace );
}


namespace CoreMl
{
	class TObject;
}

class CoreMl::TObject
{
public:
	float			mScore = 0;
	std::string		mLabel;
	Soy::Rectf		mRect = Soy::Rectf(0,0,0,0);
	vec2x<size_t>	mGridPos;
};


class CoreMl::TInstance : public SoyWorkerJobThread
{
public:
	TInstance();
	
	//	jobs
	void		RunAsync(std::shared_ptr<SoyPixelsImpl> Pixels,void(TInstance::* RunModel)(const SoyPixelsImpl&,std::function<void(const std::string&,float,Soy::Rectf)>),std::function<void()> OnFinished);
	
	
	//	synchronous funcs
	//void		RunMobileNet(const SoyPixelsImpl& Pixels,std::function<void(const std::string&,float,Soy::Rectf)> EnumObject);
	void		RunYolo(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);
	void		RunHourglass(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);
	void		RunCpm(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);
	void		RunOpenPose(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);
	void		RunSsdMobileNet(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);
	void		RunMaskRcnn(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject);

private:
	void		RunPoseModel(MLMultiArray* ModelOutput,const SoyPixelsImpl& Pixels,std::function<std::string(size_t)> GetKeypointName,std::function<void(const TObject&)> EnumObject);

private:
	//MobileNet*	mMobileNet = nullptr;
	TinyYOLO*		mTinyYolo = [[TinyYOLO alloc] init];
	hourglass*		mHourglass = [[hourglass alloc] init];
	cpm*			mCpm = [[cpm alloc] init];
	MobileOpenPose*	mOpenPose = [[MobileOpenPose alloc] init];
	SsdMobilenet*	mSsdMobileNet = [[SsdMobilenet alloc] init];
	//MaskRCNN_MaskRCNN*	mMaskRcnn = [[MaskRCNN_MaskRCNN alloc] init];
	MaskRCNN_MaskRCNN*	mMaskRcnn = nullptr;
};

CoreMl::TInstance::TInstance() :
	SoyWorkerJobThread	("CoreMl::TInstance")
{
	Start();
}

/*
void CoreMl::TInstance::RunMobileNet(const SoyPixelsImpl& Pixels,std::function<void(const std::string&,float,Soy::Rectf)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);

	CVImageBufferRef ImageBuffer = PixelBuffer;
	NSError* Error = nullptr;
	auto Output = [mMobileNet predictionFromImage:ImageBuffer error:&Error];
	
	if ( Error )
		throw Soy::AssertException( Error );
	
	throw Soy::AssertException("Enum output");
}
*/


void CoreMl::TInstance::RunYolo(const SoyPixelsImpl& Pixels,std::function<void(const TObject& Object)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc]init];
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
		[pool drain];
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );
	
	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mTinyYolo predictionFromImage:PixelBuffer error:&Error];
	Timer.Stop();
	if ( Error )
		throw Soy::AssertException( Error );

	//	parse grid output
	//	https://github.com/hollance/YOLO-CoreML-MPSNNGraph/blob/master/TinyYOLO-CoreML/TinyYOLO-CoreML/YOLO.swift
	MLMultiArray* Grid = Output.grid;
	if ( Grid.count != 125*13*13 )
	{
		std::stringstream Error;
		Error << "expected 125*13*13(21125) outputs, got " << Grid.count;
		throw Soy::AssertException(Error.str());
	}
	
	//	from https://github.com/hollance/YOLO-CoreML-MPSNNGraph/blob/a1241f3cf1fd155039c45ad4f681bb5f22897654/Common/Helpers.swift
	const std::string ClassLabels[] =
	{
		"aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat",
		"chair", "cow", "diningtable", "dog", "horse", "motorbike", "person",
		"pottedplant", "sheep", "sofa", "train", "tvmonitor"
	};
	
	
	//	from hollance/YOLO-CoreML-MPSNNGraph
	// The 416x416 image is divided into a 13x13 grid. Each of these grid cells
	// will predict 5 bounding boxes (boxesPerCell). A bounding box consists of
	// five data items: x, y, width, height, and a confidence score. Each grid
	// cell also predicts which class each bounding box belongs to.
	//
	// The "features" array therefore contains (numClasses + 5)*boxesPerCell
	// values for each grid cell, i.e. 125 channels. The total features array
	// contains 125x13x13 elements.
	
	std::function<float(int)> GetGridIndexValue;
	switch ( Grid.dataType )
	{
		case MLMultiArrayDataTypeDouble:
		GetGridIndexValue = [&](int Index)
		{
			auto* GridValues = reinterpret_cast<double*>( Grid.dataPointer );
			return static_cast<float>( GridValues[Index] );
		};
		break;
		
		case MLMultiArrayDataTypeFloat32:
		GetGridIndexValue = [&](int Index)
		{
			auto* GridValues = reinterpret_cast<float*>( Grid.dataPointer );
			return static_cast<float>( GridValues[Index] );
		};
		break;
		
		case MLMultiArrayDataTypeInt32:
		GetGridIndexValue = [&](int Index)
		{
			auto* GridValues = reinterpret_cast<int32_t*>( Grid.dataPointer );
			return static_cast<float>( GridValues[Index] );
		};
		break;
		
		default:
			throw Soy::AssertException("Unhandled grid data type");
	}
	
	auto gridHeight = 13;
	auto gridWidth = 13;
	auto blockWidth = Pixels.GetWidth() / gridWidth;
	auto blockHeight = Pixels.GetHeight() / gridHeight;
	auto boxesPerCell = 5;
	auto numClasses = 20;
	
	auto channelStride = Grid.strides[0].intValue;
	auto yStride = Grid.strides[1].intValue;
	auto xStride = Grid.strides[2].intValue;
	
	auto GetIndex = [&](int channel,int x,int y)
	{
		return channel*channelStride + y*yStride + x*xStride;
	};
	
	auto GetGridValue = [&](int channel,int x,int y)
	{
		return GetGridIndexValue( GetIndex( channel, x, y ) );
	};
	
	//	https://github.com/hollance/YOLO-CoreML-MPSNNGraph/blob/master/TinyYOLO-CoreML/TinyYOLO-CoreML/Helpers.swift#L217
	auto Sigmoid = [](float x)
	{
		return 1.0f / (1.0f + exp(-x));
	};
/*
	//	https://stackoverflow.com/a/10733861/355753
	auto Sigmoid = [](float x)
	{
		return x / (1.0f + abs(x));
	};
	*/
	for ( auto cy=0;	cy<gridHeight;	cy++ )
	for ( auto cx=0;	cx<gridWidth;	cx++ )
	for ( auto b=0;	b<boxesPerCell;	b++ )
	{
		// For the first bounding box (b=0) we have to read channels 0-24,
		// for b=1 we have to read channels 25-49, and so on.
		auto channel = b*(numClasses + 5);
		auto tx = GetGridValue(channel    , cx, cy);
		auto ty = GetGridValue(channel + 1, cx, cy);
		auto tw = GetGridValue(channel + 2, cx, cy);
		auto th = GetGridValue(channel + 3, cx, cy);
		auto tc = GetGridValue(channel + 4, cx, cy);
		
		// The confidence value for the bounding box is given by tc. We use
		// the logistic sigmoid to turn this into a percentage.
		auto confidence = Sigmoid(tc);
		if ( confidence < 0.01f )
			continue;
		//std::Debug << "Confidence: " << confidence << std::endl;
		
		// The predicted tx and ty coordinates are relative to the location
		// of the grid cell; we use the logistic sigmoid to constrain these
		// coordinates to the range 0 - 1. Then we add the cell coordinates
		// (0-12) and multiply by the number of pixels per grid cell (32).
		// Now x and y represent center of the bounding box in the original
		// 416x416 image space.
		auto RectCenterx = (cx + Sigmoid(tx)) * blockWidth;
		auto RectCentery = (cy + Sigmoid(ty)) * blockHeight;
		
		// The size of the bounding box, tw and th, is predicted relative to
		// the size of an "anchor" box. Here we also transform the width and
		// height into the original 416x416 image space.
		//	let anchors: [Float] = [1.08, 1.19, 3.42, 4.41, 6.63, 11.38, 9.42, 5.11, 16.62, 10.52]
		float anchors[] = { 1.08, 1.19, 3.42, 4.41, 6.63, 11.38, 9.42, 5.11, 16.62, 10.52 };
		auto RectWidth = exp(tw) * anchors[2*b    ] * blockWidth;
		auto RectHeight = exp(th) * anchors[2*b + 1] * blockHeight;
		//auto RectWidth = exp(tw) * blockWidth;
		//auto RectHeight = exp(th) * blockHeight;
		
		
		//	iterate through each class
		float BestClassScore = 0;
		int BestClassIndex = -1;
		for ( auto ClassIndex=0;	ClassIndex<numClasses;	ClassIndex++)
		{
			auto Score = GetGridValue( channel + 5 + ClassIndex, cx, cy );
			if ( ClassIndex > 0 && Score < BestClassScore )
				continue;
	
			BestClassIndex = ClassIndex;
			BestClassScore = Score;
		}
		//auto Score = BestClassScore * confidence;
		auto Score = confidence;
		auto& Label = ClassLabels[BestClassIndex];
		
		auto x = RectCenterx - (RectWidth/2);
		auto y = RectCentery - (RectHeight/2);
		auto w = RectWidth;
		auto h = RectHeight;
		//	normalise output
		x /= Pixels.GetWidth();
		y /= Pixels.GetHeight();
		w /= Pixels.GetWidth();
		h /= Pixels.GetHeight();
		
		TObject Object;
		Object.mLabel = Label;
		Object.mScore = Score;
		Object.mRect = Soy::Rectf( x,y,w,h );
		EnumObject( Object );
	}
}


void NSArray_NSNumber_ForEach(NSArray<NSNumber*>* Numbers,std::function<void(int64_t)> Enum)
{
	auto Size = [Numbers count];
	for ( auto i=0;	i<Size;	i++ )
	{
		auto* Num = [Numbers objectAtIndex:i];
		auto Integer = [Num integerValue];
		int64_t Integer64 = Integer;
		Enum( Integer64 );
	}
}

template<typename SOURCETYPE,typename NUMBERTYPE>
void CopyValuesFromVoid(ArrayBridge<NUMBERTYPE>& Dest,void* Source,size_t Count)
{
	auto* SourceValues = reinterpret_cast<SOURCETYPE*>( Source );
	//Dest.SetSize( Count );
	
	auto SourceArray = GetRemoteArray( SourceValues, Count );
	Dest.Copy( SourceArray );
	/*
	//	for speed
	auto* DestPtr = Dest.GetArray();
	for ( auto i=0;	i<Count;	i++ )
	{
		auto Sourcef = static_cast<float>( SourceValues[i] );
		DestPtr[i] = Sourcef;
	}
*/
}

template<typename NUMBER>
void ExtractFloatsFromMultiArray(MLMultiArray* MultiArray,ArrayBridge<int>&& Dimensions,ArrayBridge<NUMBER>&& Values)
{
	Soy::TScopeTimerPrint Timer("ExtractFloatsFromMultiArray", 4);
	
	
	//	get dimensions
	auto PushDim = [&](int64_t DimSize)
	{
		Dimensions.PushBack( static_cast<int>(DimSize) );
	};
	NSArray_NSNumber_ForEach( MultiArray.shape, PushDim );

	//	todo: this should match dim
	auto ValueCount = MultiArray.count;

	//	convert all values now
	void* MultiArray_DataPointer = MultiArray.dataPointer;
	switch ( MultiArray.dataType )
	{
		case MLMultiArrayDataTypeDouble:
			CopyValuesFromVoid<double>( Values, MultiArray_DataPointer, ValueCount );
			break;
			
		case MLMultiArrayDataTypeFloat32:
			CopyValuesFromVoid<float>( Values, MultiArray_DataPointer, ValueCount );
			break;
			
		case MLMultiArrayDataTypeInt32:
			CopyValuesFromVoid<int32_t>( Values, MultiArray_DataPointer, ValueCount );
			break;
			
		default:
			throw Soy::AssertException("Unhandled grid data type");
	}
	
}


void CoreMl::TInstance::RunHourglass(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc]init];
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
		[pool drain];
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );

	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mHourglass predictionFromImage__0:PixelBuffer error:&Error];
	Timer.Stop();
	if ( Error )
		throw Soy::AssertException( Error );

	//	https://github.com/tucan9389/PoseEstimation-CoreML/blob/master/PoseEstimation-CoreML/JointViewController.swift#L230
	const std::string KeypointLabels[] =
	{
		"Head", "Neck",
		"RightShoulder", "RightElbow", "RightWrist",
		"LeftShoulder", "LeftElbow", "LeftWrist",
		"RightHip", "RightKnee", "RightAnkle",
		"LeftHip", "LeftKnee", "LeftAnkle",
	};

	auto GetKeypointName = [&](size_t Index)
	{
		return KeypointLabels[Index];
	};
	
	RunPoseModel( Output.hourglass_out_3__0, Pixels, GetKeypointName, EnumObject );
}

void CoreMl::TInstance::RunCpm(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc]init];
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
		[pool drain];
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );

	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mCpm predictionFromImage__0:PixelBuffer error:&Error];
	Timer.Stop();
	if ( Error )
		throw Soy::AssertException( Error );
	
	//	https://github.com/tucan9389/PoseEstimation-CoreML/blob/master/PoseEstimation-CoreML/JointViewController.swift#L230
	const std::string KeypointLabels[] =
	{
		"Head", "Neck",
		"RightShoulder", "RightElbow", "RightWrist",
		"LeftShoulder", "LeftElbow", "LeftWrist",
		"RightHip", "RightKnee", "RightAnkle",
		"LeftHip", "LeftKnee", "LeftAnkle",
	};
	auto GetKeypointName = [&](size_t Index)
	{
		return KeypointLabels[Index];
	};

	RunPoseModel( Output.Convolutional_Pose_Machine__stage_5_out__0, Pixels, GetKeypointName, EnumObject );
}


void CoreMl::TInstance::RunOpenPose(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc]init];
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
		[pool drain];
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );

	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mOpenPose predictionFromImage:PixelBuffer error:&Error];
	Timer.Stop();
	if ( Error )
		throw Soy::AssertException( Error );
	
	//	https://github.com/infocom-tpo/SwiftOpenPose/blob/9745c0074dfe7d98265a325e25d2e2bb3d91d3d1/SwiftOpenPose/Sources/Estimator.swift#L122
	//	heatmap rowsxcols is width/8 and height/8 which is 48, which is dim[1] and dim[2]
	//	but 19 features? (dim[0] = 3*19)
	//	https://github.com/tucan9389/PoseEstimation-CoreML/blob/master/PoseEstimation-CoreML/JointViewController.swift#L230
	//	https://github.com/eugenebokhan/iOS-OpenPose/blob/master/iOSOpenPose/iOSOpenPose/CoreML/CocoPairs.swift#L12
	const std::string KeypointLabels[] =
	{
		"Head", "Neck",
		"RightShoulder", "RightElbow", "RightWrist",
		"LeftShoulder", "LeftElbow", "LeftWrist",
		"RightHip", "RightKnee", "RightAnkle",
		"LeftHip", "LeftKnee", "LeftAnkle",
		"RightEye",
		"LeftEye",
		"RightEar",
		"LeftEar",
		"Background"
	};
	auto BackgroundLabelIndex = 18;
	auto GetKeypointName = [&](size_t Index)
	{
		if ( Index < sizeofarray(KeypointLabels) )
			return KeypointLabels[Index];
		
		std::stringstream KeypointName;
		KeypointName << "Label_" << Index;
		return KeypointName.str();
	};
	
	auto* ModelOutput = Output.net_output;
	//RunPoseModel( Output.net_output, Pixels, GetKeypointName, EnumObject );

	if ( !ModelOutput )
		throw Soy::AssertException("No output from model");
	
	using NUMBER = double;
	BufferArray<int,10> Dim;
	Array<NUMBER> Values;
	ExtractFloatsFromMultiArray( ModelOutput, GetArrayBridge(Dim), GetArrayBridge(Values) );
	
	auto KeypointCount = Dim[0];
	auto HeatmapWidth = Dim[1];
	auto HeatmapHeight = Dim[2];
	auto GetValue = [&](int Keypoint,int HeatmapX,int HeatmapY)
	{
		auto Index = Keypoint * (HeatmapWidth*HeatmapHeight);
		Index += HeatmapX*(HeatmapHeight);
		Index += HeatmapY;
		return Values[Index];
	};
	
	//	same as dim
	//	heatRows = imageWidth / 8
	//	heatColumns = imageHeight / 8
	//	KeypointCount is 57 = 19 + 38
	auto HeatRows = HeatmapWidth;
	auto HeatColumns = HeatmapHeight;

	//	https://github.com/infocom-tpo/SwiftOpenPose/blob/9745c0074dfe7d98265a325e25d2e2bb3d91d3d1/SwiftOpenPose/Sources/Estimator.swift#L127
	//	https://github.com/eugenebokhan/iOS-OpenPose/blob/master/iOSOpenPose/iOSOpenPose/CoreML/PoseEstimatior.swift#L72
	//	code above splits into pafMat and HeatmapMat
	auto HeatMatRows = 19;
	auto HeatMatCols = HeatRows*HeatColumns;
	auto heatMat_count = HeatMatRows * HeatMatCols;
	/*
	NUMBER heatMat[HeatMatRows * HeatMatCols];//[19*HeatRows*HeatColumns];
	NUMBER pafMat[Values.GetSize() - sizeofarray(heatMat)];//[38*HeatRows*HeatColumns];
	//let heatMat = Matrix<Double>(rows: 19, columns: heatRows*heatColumns, elements: data )
	//let separateLen = 19*heatRows*heatColumns
	//let pafMat = Matrix<Double>(rows: 38, columns: heatRows*heatColumns, elements: Array<Double>(mm[separateLen..<mm.count]))
	auto heatMat_count = sizeofarray(heatMat);
	for ( auto i=0;	i<heatMat_count;	i++ )
		heatMat[i] = Values[i];
	auto pafMat_count = sizeofarray(pafMat);
	for ( auto i=0;	i<pafMat_count;	i++ )
		pafMat[i] = Values[heatMat_count+i];
	*/
	auto* heatMat = Values.GetArray();
	auto* pafMat = &heatMat[heatMat_count];
	
	//	pull coords from heat map
	using vec2i = vec2x<int32_t>;
	Array<vec2i> Coords;
	auto PushCoord = [&](int KeypointIndex,int Index,float Score)
	{
		if ( Score < 0.01f )
			return;
		
		auto y = Index / HeatRows;
		auto x = Index % HeatRows;
		Coords.PushBack( vec2i(x,y) );
		
		auto Label = GetKeypointName(KeypointIndex);
		auto xf = x / static_cast<float>(HeatRows);
		auto yf = y / static_cast<float>(HeatColumns);
		auto wf = 1 / static_cast<float>(HeatRows);
		auto hf = 1 / static_cast<float>(HeatColumns);
		auto Rect = Soy::Rectf( xf, yf, wf, hf );
		
		TObject Object;
		Object.mLabel = Label;
		Object.mScore = Score;
		Object.mRect = Rect;
		Object.mGridPos.x = x;
		Object.mGridPos.y = y;
		EnumObject( Object );
	};
	
	for ( auto r=0;	r<HeatMatRows;	r++ )
	{
		auto KeypointIndex = r;
		static bool SkipBackground = true;
		if ( SkipBackground && KeypointIndex == BackgroundLabelIndex )
			continue;
		auto nms = GetRemoteArray( &heatMat[r*HeatMatCols], HeatMatCols );
		/*
		std::Debug << "r=" << r << " [ ";
		for ( int i=0;	i<nms.GetSize();	i++ )
		{
			int fi = nms[i] * 100;
			std::Debug << fi << " ";
		}
		std::Debug << " ]" << std::endl;
		*/
		//	get biggest in nms
		//	gr: I think the original code goes through and gets rid of the not-biggest
		//		or only over the threshold?
		//		but the swift code is then doing the check twice if that's the case (and doest need the opencv filter at all)
		/*
		openCVWrapper.maximum_filter(&data,
									 data_size: Int32(data.count),
									 data_rows: dataRows,
									 mask_size: maskSize,
									 threshold: threshold)
		 */
		
		
		
		static bool BestOnly = false;		
		if ( BestOnly )
		{
			auto BiggestCol = 0;
			auto BiggestVal = -1.0f;
			for ( auto c=0;	c<nms.GetSize();	c++ )
			{
				if ( nms[c] <= BiggestVal )
					continue;
				BiggestCol = c;
				BiggestVal = nms[c];
			}
			
			PushCoord( KeypointIndex, BiggestCol, BiggestVal );
		}
		else
		{
			for ( auto c=0;	c<nms.GetSize();	c++ )
			{
				PushCoord( KeypointIndex, c, nms[c] );
			}
		}
		
	}
	/*
	var coords = [[(Int,Int)]]()
	for i in 0..<heatMat.rows-1
	 {
		 var nms = Array<Double>(heatMat.row(i))
	 	nonMaxSuppression(&nms, dataRows: Int32(heatColumns), maskSize: 5, threshold: _nmsThreshold)
	 	 let c = nms.enumerated().filter{ $0.1 > _nmsThreshold }.map
	 		{
	 	x in
			 return ( x.0 / heatRows , x.0 % heatRows )
	 		}
	 	coords.append(c)
	 }
	*/

}


void CoreMl::TInstance::RunPoseModel(MLMultiArray* ModelOutput,const SoyPixelsImpl& Pixels,std::function<std::string(size_t)> GetKeypointName,std::function<void(const TObject&)> EnumObject)
{
	if ( !ModelOutput )
		throw Soy::AssertException("No output from model");

	BufferArray<int,10> Dim;
	Array<float> Values;
	ExtractFloatsFromMultiArray( ModelOutput, GetArrayBridge(Dim), GetArrayBridge(Values) );
	
	//	parse Hourglass data
	//	https://github.com/tucan9389/PoseEstimation-CoreML/blob/master/PoseEstimation-CoreML/JointViewController.swift#L135
	auto KeypointCount = Dim[0];
	auto HeatmapWidth = Dim[2];
	auto HeatmapHeight = Dim[1];
	auto GetValue = [&](int Keypoint,int HeatmapX,int HeatmapY)
	{
		auto Index = Keypoint * (HeatmapWidth*HeatmapHeight);
		//Index += HeatmapX*(HeatmapHeight);
		//Index += HeatmapY;
		Index += HeatmapY*(HeatmapWidth);
		Index += HeatmapX;
		return Values[Index];
	};

	
	static bool EnumAllResults = true;
	auto MinConfidence = 0.01f;
	
	for ( auto k=0;	k<KeypointCount;	k++)
	{
		auto KeypointLabel = GetKeypointName(k);
		TObject BestObject;
		BestObject.mScore = -1;
		
		auto EnumKeypoint = [&](const std::string& Label,float Score,const Soy::Rectf& Rect,size_t GridX,size_t GridY)
		{
			TObject Object;
			Object.mLabel = Label;
			Object.mScore = Score;
			Object.mRect = Rect;
			Object.mGridPos.x = GridX;
			Object.mGridPos.y = GridY;
			
			if ( EnumAllResults )
			{
				EnumObject( Object );
				return;
			}
			if ( Score < BestObject.mScore )
				return;
			BestObject = Object;
		};

		for ( auto x=0;	x<HeatmapWidth;	x++)
		{
			for ( auto y=0;	y<HeatmapHeight;	y++)
			{
				auto Confidence = GetValue( k, x, y );
				if ( Confidence < MinConfidence )
					continue;
				
				auto xf = x / static_cast<float>(HeatmapWidth);
				auto yf = y / static_cast<float>(HeatmapHeight);
				auto wf = 1 / static_cast<float>(HeatmapWidth);
				auto hf = 1 / static_cast<float>(HeatmapHeight);
				auto Rect = Soy::Rectf( xf, yf, wf, hf );
				EnumKeypoint( KeypointLabel, Confidence, Rect, x, y );
			}
		}
		
		//	if only outputting best, do it
		if ( !EnumAllResults && BestObject.mScore > 0 )
		{
			EnumObject( BestObject );
		}
		
	}

}


void CoreMl::TInstance::RunSsdMobileNet(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	NSAutoreleasePool* pool = [[NSAutoreleasePool alloc]init];
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
		[pool drain];
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );

	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mSsdMobileNet predictionFromPreprocessor__sub__0:PixelBuffer error:&Error];
	Timer.Stop();
	if ( !Output )
		throw Soy::AssertException("No output from CoreMl prediction");
	if ( Error )
		throw Soy::AssertException( Error );
	
	//	https://github.com/vonholst/SSDMobileNet_CoreML/tree/master/SSDMobileNet/SSDMobileNet
	//	https://github.com/vonholst/SSDMobileNet_CoreML/blob/master/SSDMobileNet/SSDMobileNet/coco_labels_list.txt
	const std::string KeypointLabels[] =
	{
		"background","person","bicycle","car","motorcycle","airplane","bus",
		"train","truck","boat","traffic light","fire hydrant","???","stop sign","parking meter",
		"bench","bird","cat","dog","horse","sheep","cow","elephant","bear","zebra",
		"giraffe","???","backpack","umbrella","???","???","handbag","tie","suitcase",
		"frisbee","skis","snowboard","sports ball","kite","baseball bat","baseball glove",
		"skateboard","surfboard","tennis racket","bottle","???","wine glass","cup",
		"fork","knife","spoon","bowl","banana","apple","sandwich","orange","broccoli",
		"carrot","hot dog","pizza","donut","cake","chair","couch","potted plant",
		"bed","???","dining table","???","???","toilet","???","tv","laptop","mouse",
		"remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator",
		"???","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"
	};
	auto BackgroundClassIndex = 0;
	auto GetKeypointName = [&](size_t Index)
	{
		if ( Index < sizeofarray(KeypointLabels) )
		return KeypointLabels[Index];
		
		std::stringstream KeypointName;
		KeypointName << "Label_" << Index;
		return KeypointName.str();
	};
	
	
	//	parse SSD Mobilenet data;
	//	https://github.com/tf-coreml/tf-coreml/issues/107#issuecomment-359675509
	//	Scores for each class (concat_1__0, a 1 x 1 x 91 x 1 x 1917 MLMultiArray)
	//	Anchor-encoded Boxes (concat__0, a 1 x 1 x 4 x 1 x 1917 MLMultiArray)
	using NUMBER = double;
	BufferArray<int,10> ClassScores_Dim;
	Array<NUMBER> ClassScores_Values;
	BufferArray<int,10> AnchorBoxes_Dim;
	Array<NUMBER> AnchorBoxes_Values;
	ExtractFloatsFromMultiArray( Output.concat__0, GetArrayBridge(AnchorBoxes_Dim), GetArrayBridge(AnchorBoxes_Values) );
	ExtractFloatsFromMultiArray( Output.concat_1__0, GetArrayBridge(ClassScores_Dim), GetArrayBridge(ClassScores_Values) );
/*
	std::Debug << "Class Scores: [ ";
	for ( int i=0;	i<ClassScores_Dim.GetSize();	i++ )
		std::Debug << ClassScores_Dim[i] << "x";
	std::Debug << " ]" << std::endl;
*/
	//	Scores for each class (concat_1__0, a 1 x 1 x 91 x 1 x 1917 MLMultiArray)
	//	1917 bounding boxes, 91 classes
	int ClassCount = ClassScores_Dim[2];
	int BoxCount = ClassScores_Dim[4];
	if ( ClassCount != 91 )		throw Soy::AssertException("Expected 91 classes");
	if ( BoxCount != 1917 )	throw Soy::AssertException("Expected 1917 boxes");
	class TClassAndBox
	{
	public:
		float	mScore;
		size_t	mBoxIndex;
		size_t	mClassIndex;
	};
	auto GetBoxClassScore = [&](size_t BoxIndex,size_t ClassIndex)
	{
		auto ValueIndex = (ClassIndex * BoxCount) + BoxIndex;
		auto Score = ClassScores_Values[ValueIndex];
		if ( Score < static_cast<NUMBER>(0.01f) )
			return static_cast<NUMBER>(0.0f);
		return Score;
	};
	Array<TClassAndBox> Matches;
	for ( auto b=0;	b<BoxCount;	b++ )
	{
		//std::Debug << "Box[" << b << "] = [";
		for ( auto c=0;	c<ClassCount;	c++ )
		{
			bool SkipBackground = true;
			if ( SkipBackground && c == BackgroundClassIndex )
				continue;
			
			auto Score = GetBoxClassScore( b, c );
			//std::Debug << " " << int( Score * 100 );
			if ( Score <= 0 )
				continue;
			
			TClassAndBox Match;
			Match.mScore = Score;
			Match.mBoxIndex = b;
			Match.mClassIndex = c;
			Matches.PushBack( Match );
		}
		//std::Debug << " ]" << std::endl;
	}
	
	auto& Anchors = CoreMl::SsdMobileNet_AnchorBounds4;
	auto GetAnchorRect = [&](size_t AnchorIndex)
	{
		//	from here	https://gist.github.com/vincentchu/cf507ed013da0e323d689bd89119c015#file-ssdpostprocessor-swift-L123
		//	I believe the order is y,x,h,w
		auto MinX = Anchors[AnchorIndex][1];
		auto MinY = Anchors[AnchorIndex][0];
		auto MaxX = Anchors[AnchorIndex][3];
		auto MaxY = Anchors[AnchorIndex][2];
		return Soy::Rectf( MinX, MinY, MaxX - MinX, MaxY-MinY );
	};

	//	Anchor-encoded Boxes (concat__0, a 1 x 1 x 4 x 1 x 1917 MLMultiArray)
	auto GetAnchorEncodedRect = [&](size_t BoxIndex)
	{
		//	https://github.com/tf-coreml/tf-coreml/issues/107#issuecomment-359675509
		//	The output of the k-th CoreML box is:
		//	ty, tx, th, tw = boxes[0, 0, :, 0, k]
		//	https://github.com/tensorflow/models/blob/master/research/object_detection/box_coders/keypoint_box_coder.py#L128
		//	where x, y, w, h denote the box's center coordinates, width and height
		//	respectively. Similarly, xa, ya, wa, ha denote the anchor's center
		auto Index_y = (0 * BoxCount) + BoxIndex;
		auto Index_x = (1 * BoxCount) + BoxIndex;
		auto Index_h = (2 * BoxCount) + BoxIndex;
		auto Index_w = (3 * BoxCount) + BoxIndex;
		auto x = AnchorBoxes_Values[Index_x];
		auto y = AnchorBoxes_Values[Index_y];
		auto w = AnchorBoxes_Values[Index_w];
		auto h = AnchorBoxes_Values[Index_h];
		
		//	center to top left
		x -= w/2.0f;
		y -= h/2.0f;
		return Soy::Rectf( x, y, w, h );
	};

	auto GetAnchorDecodedRect = [&](size_t BoxIndex)
	{
		auto AnchorRect = GetAnchorRect( BoxIndex );
		auto EncodedRect = GetAnchorEncodedRect( BoxIndex );
		
		//	https://github.com/tensorflow/models/blob/master/research/object_detection/box_coders/keypoint_box_coder.py#L128
		//	^ this is the ENCODING of the values, so we need to reverse it
		/*
		 ty = (y - ya) / ha
		 tx = (x - xa) / wa
		 th = log(h / ha)
		 tw = log(w / wa)
		 tky0 = (ky0 - ya) / ha
		 tkx0 = (kx0 - xa) / wa
		 tky1 = (ky1 - ya) / ha
		 tkx1 = (kx1 - xa) / wa
		 */
		//	https://gist.github.com/vincentchu/cf507ed013da0e323d689bd89119c015#file-ssdpostprocessor-swift-L47
		//	You take these and combine them with the anchor boxes using the same routine as this python code.
		//	https://github.com/tensorflow/models/blob/master/research/object_detection/box_coders/keypoint_box_coder.py#L128
		//	Note: You'll need to use the scale_factors of 10.0 and 5.0 here.
		
		//	this is the decoding;
		//	https://gist.github.com/vincentchu/cf507ed013da0e323d689bd89119c015#file-ssdpostprocessor-swift-L47
		auto axcenter = AnchorRect.GetCenterX();
		auto aycenter = AnchorRect.GetCenterY();
		auto ha = AnchorRect.GetHeight();
		auto wa = AnchorRect.GetWidth();
		
		auto ty = EncodedRect.y / 10.0f;
		auto tx = EncodedRect.x / 10.0f;
		auto th = EncodedRect.GetHeight() / 5.0f;
		auto tw = EncodedRect.GetWidth() / 5.0f;
		
		auto w = exp(tw) * wa;
		auto h = exp(th) * ha;
		auto yCtr = ty * ha + aycenter;
		auto xCtr = tx * wa + axcenter;
		auto x = xCtr - (w/2.0f);
		auto y = yCtr - (h/2.0f);
		return Soy::Rectf( x, y, w, h );
	};
	
	//	now extract boxes for matches
	for ( auto i=0;	i<Matches.GetSize();	i++ )
	{
		auto& Match = Matches[i];
		
		auto BoxIndex = Match.mBoxIndex;
		auto Box = GetAnchorDecodedRect( BoxIndex );
		auto ClassName = GetKeypointName( Match.mClassIndex );
		
		TObject Object;
		Object.mLabel = ClassName;
		Object.mScore = Match.mScore;
		Object.mRect = Box;
		EnumObject( Object );
	}
	/*
	
	std::Debug << "Anchor Boxes: [ ";
	for ( int i=0;	i<AnchorBoxes_Dim.GetSize();	i++ )
		std::Debug << AnchorBoxes_Dim[i] << "x";
	std::Debug << " ]" << std::endl;
	*/
}


void CoreMl::TInstance::RunMaskRcnn(const SoyPixelsImpl& Pixels,std::function<void(const TObject&)> EnumObject)
{
	auto PixelBuffer = Avf::PixelsToPixelBuffer(Pixels);
	auto ReleasePixelBuffer = [&]()
	{
		CVPixelBufferRelease(PixelBuffer);
	};
	Soy::TScopeCall ReleasePixels( nullptr, ReleasePixelBuffer );
	NSError* Error = nullptr;
	
	Soy::TScopeTimerPrint Timer(__func__,0);
	auto Output = [mMaskRcnn predictionFromImage:PixelBuffer error:&Error];
		Timer.Stop();
	if ( Error )
		throw Soy::AssertException( Error );
	if ( !Output )
		throw Soy::AssertException("No output from CoreMl prediction");

	/// Detections (y1,x1,y2,x2,classId,score) as 6 element vector of doubles
	BufferArray<int,10> ClassBox_Dim;
	Array<float> ClassBox_Values;
	ExtractFloatsFromMultiArray( Output.detections, GetArrayBridge(ClassBox_Dim), GetArrayBridge(ClassBox_Values) );

	std::Debug << "ClassBox_Dim: [ ";
	for ( int i=0;	i<ClassBox_Dim.GetSize();	i++ )
		std::Debug << ClassBox_Dim[i] << "x";
	std::Debug << " ]" << std::endl;


	throw Soy::AssertException("Process RCNN output");
}

void TCoreMlWrapper::Construct(Bind::TCallback& Arguments)
{
	mCoreMl.reset( new CoreMl::TInstance );
}

void TCoreMlWrapper::CreateTemplate(Bind::TTemplate& Template)
{
	Template.BindFunction<Yolo_FunctionName>( Yolo );
	Template.BindFunction<Hourglass_FunctionName>( Hourglass );
	Template.BindFunction<Cpm_FunctionName>( Cpm );
	Template.BindFunction<OpenPose_FunctionName>( OpenPose );
	Template.BindFunction<SsdMobileNet_FunctionName>( SsdMobileNet );
	Template.BindFunction<MaskRcnn_FunctionName>( MaskRcnn );
	Template.BindFunction<FaceDetect_FunctionName>( FaceDetect );
}


template<typename COREML_FUNC>
void RunModel(COREML_FUNC CoreMlFunc,Bind::TCallback& Params,std::shared_ptr<CoreMl::TInstance> CoreMl)
{
	auto* pImage = &Params.GetArgumentPointer<TImageWrapper>(0);
	auto Promise = Params.mContext.CreatePromise(__FUNCTION__);
	auto* pContext = &Params.mContext;
	
	auto RunModel = [=]
	{
		try
		{
			//	do all the work on the thread
			auto& Image = *pImage;
			auto& CurrentPixels = Image.GetPixels();
			SoyPixels TempPixels;
			SoyPixelsImpl* pPixels = &TempPixels;
			if ( CurrentPixels.GetFormat() == SoyPixelsFormat::RGBA )
			{
				pPixels = &CurrentPixels;
			}
			else
			{
				pImage->GetPixels(TempPixels);
				TempPixels.SetFormat( SoyPixelsFormat::RGBA );
				pPixels = &TempPixels;
			}
			auto& Pixels = *pPixels;
			
			Array<CoreMl::TObject> Objects;
			
			auto PushObject = [&](const CoreMl::TObject& Object)
			{
				Objects.PushBack(Object);
			};
			CoreMlFunc( *CoreMl, Pixels, PushObject );
			
			auto OnCompleted = [=](Bind::TContext& Context)
			{
				auto GetElement = [&](size_t Index)
				{
					auto& Object = Objects[Index];
					auto ObjectJs = Context.CreateObjectInstance();
					ObjectJs.SetString("Label", Object.mLabel );
					ObjectJs.SetFloat("Score", Object.mScore );
					ObjectJs.SetFloat("x", Object.mRect.x );
					ObjectJs.SetFloat("y", Object.mRect.y );
					ObjectJs.SetFloat("w", Object.mRect.w );
					ObjectJs.SetFloat("h", Object.mRect.h );
					ObjectJs.SetInt("GridX", Object.mGridPos.x );
					ObjectJs.SetInt("GridY", Object.mGridPos.y );
					return ObjectJs;
				};
				auto ObjectsArray = Context.CreateArray( Objects.GetSize(), GetElement );
				Promise.Resolve( ObjectsArray );
			};
			
			pContext->Queue( OnCompleted );
		}
		catch(std::exception& e)
		{
			//	queue the error callback
			std::string ExceptionString(e.what());
			Promise.Reject( ExceptionString );
		}
	};
	
	CoreMl->PushJob(RunModel);
	
	Params.Return( Promise );
}


void TCoreMlWrapper::Yolo(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunYolo );
	return RunModel( CoreMlFunc, Params, CoreMl );
}


void TCoreMlWrapper::Hourglass(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunHourglass );
	return RunModel( CoreMlFunc, Params, CoreMl );
}



void TCoreMlWrapper::Cpm(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunCpm );
	return RunModel( CoreMlFunc, Params, CoreMl );
}



void TCoreMlWrapper::OpenPose(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunOpenPose );
	return RunModel( CoreMlFunc, Params, CoreMl );
}



void TCoreMlWrapper::SsdMobileNet(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunSsdMobileNet );
	return RunModel( CoreMlFunc, Params, CoreMl );
}


void TCoreMlWrapper::MaskRcnn(Bind::TCallback& Params)
{
	auto& This = Params.This<TCoreMlWrapper>();
	auto& CoreMl = This.mCoreMl;

	auto CoreMlFunc = std::mem_fn( &CoreMl::TInstance::RunMaskRcnn );
	return RunModel( CoreMlFunc, Params, CoreMl );
}


//	apple's Vision built-in face detection
void TCoreMlWrapper::FaceDetect(Bind::TCallback& Params)
{
	Soy::TScopeTimerPrint FaceDetectTimer("TCoreMlWrapper::FaceDetect", 10);
	auto& Image = Params.GetArgumentPointer<TImageWrapper>(0);
	std::shared_ptr<SoyPixels> Pixels( new SoyPixels() );
	Image.GetPixels( *Pixels );
	Pixels->SetFormat( SoyPixelsFormat::Greyscale );
	Pixels->SetFormat( SoyPixelsFormat::RGBA );
	auto Promise = Params.mContext.CreatePromise(__FUNCTION__);

	auto Run = [=](Bind::TContext& Context)
	{
		//	make a face request
		VNDetectFaceLandmarksRequest* Request = [[VNDetectFaceLandmarksRequest alloc] init];
		VNSequenceRequestHandler* Handler = [[VNSequenceRequestHandler alloc] init];
		NSArray<VNDetectFaceLandmarksRequest*>* Requests = @[Request];

		auto PixelBuffer = Avf::PixelsToPixelBuffer(*Pixels);
		CVImageBufferRef ImageBuffer = PixelBuffer;

		auto Orientation = kCGImagePropertyOrientationUp;
		NSError* Error = nullptr;
		{
			Soy::TScopeTimerPrint Timer("Perform Requests",5);
			[Handler performRequests:Requests onCVPixelBuffer:ImageBuffer orientation:Orientation error:&Error];
		}
		NSArray<VNFaceObservation*>* Results = Request.results;
		
		if ( !Results )
			throw Soy::AssertException("Missing results");

		Array<Bind::TObject> ResultObjects;
		for ( auto r=0;	r<Results.count;	r++ )
		{
			for ( VNFaceObservation* Observation in Results )
			{
				std::Debug << "Got observation" << std::endl;
				//	features are normalised to bounds
				Array<vec2f> Features;
				Array<float> FeatureFloats;
				Soy::Rectf Bounds;
				
				auto FlipNormalisedY = [](float y,float h)
				{
					auto Bottom = y + h;
					return 1 - Bottom;
				};
				
				VNFaceLandmarkRegion2D* Landmarks = Observation.landmarks.allPoints;
				Bounds.x = Observation.boundingBox.origin.x;
				Bounds.y = Observation.boundingBox.origin.y;
				Bounds.w = Observation.boundingBox.size.width;
				Bounds.h = Observation.boundingBox.size.height;

				for( auto l=0;	l<Landmarks.pointCount;	l++ )
				{
					auto Point = Landmarks.normalizedPoints[l];
					//	gr: this may be upside down as bounds are
					Features.PushBack( vec2f(Point.x,Point.y) );
					FeatureFloats.PushBack( Point.x );
					FeatureFloats.PushBack( Point.y );
				}
			
				auto w = Pixels->GetWidth();
				auto h = Pixels->GetHeight();
				BufferArray<float,4> RectValues;
				RectValues.PushBack( Bounds.x );
				RectValues.PushBack( FlipNormalisedY(Bounds.y,Bounds.h) );
				RectValues.PushBack( Bounds.w );
				RectValues.PushBack( Bounds.h );
				
				auto Object = Context.CreateObjectInstance();
				Object.SetArray("Bounds", GetArrayBridge(RectValues) );
				Object.SetArray("Features", GetArrayBridge(FeatureFloats) );
				ResultObjects.PushBack(Object);
			}
		}
		
		auto Array = Context.CreateArray( GetArrayBridge(ResultObjects) );
		Promise.Resolve( Array );
	};
	
	Run( Params.mContext );
	
	Params.Return( Promise );
}


