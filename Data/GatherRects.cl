#define const	__constant
#pragma OPENCL EXTENSION cl_khr_local_int32_base_atomics : enable

#define DECLARE_DYNAMIC_ARRAY(TYPE)		\
typedef struct							\
{										\
	__global TYPE*			mData;		\
	volatile __global int*	mOffset;	\
	int				mMax;		\
} TArray_ ## TYPE;						\
\
static bool PushArrayGetIndex_ ## TYPE(TArray_ ## TYPE Array,const TYPE* Value,int* pNewIndex)	{		\
	int NewIndex = atomic_inc( Array.mOffset );	/*get next unused index*/	\
	bool Success = (NewIndex < Array.mMax);		/*out of space*/			\
	NewIndex = min( NewIndex, Array.mMax-1 );	/* dont go out of bounds */	\
	Array.mData[NewIndex] = *Value;			\
	*pNewIndex = NewIndex;				\
	return Success;		\
}						\
static bool PushArray_ ## TYPE(TArray_ ## TYPE Array,TYPE* Value)	{	\
	int NewIndex = atomic_inc( Array.mOffset );	/*get next unused index*/	\
	bool Success = (NewIndex < Array.mMax);		/*out of space*/			\
	NewIndex = min( NewIndex, Array.mMax-1 );	/* dont go out of bounds */	\
	Array.mData[NewIndex] = *Value;			\
	return Success;		\
}						\

DECLARE_DYNAMIC_ARRAY(float4);

//	max from writing
const float2 MaxRectSize = (float2)(80,100);

//	filters
const float2 MinRectSize = (float2)(5,5);
const float2 MinAlignment = (float2)( 0.4f, 0.85f );
const float2 MaxAlignment = (float2)( 0.5f, 1.0f );


static float4 GetMinMax(int2 SampleCoord,float4 Sample)
{
	float2 SampleCoordf = (float2)(SampleCoord.x,SampleCoord.y);
	float2 HalfMaxRectSize = MaxRectSize / 2.f;
	float2 Min = Sample.xy * -1.f;
	float2 Max = Sample.zw;
	Min *= HalfMaxRectSize;
	Max *= HalfMaxRectSize;
	
	Min += SampleCoordf;
	Max += SampleCoordf;
	
	return (float4)(Min.x,Min.y,Max.x,Max.y);
}

static float2 GetMinMaxSize(float4 MinMax)
{
	return (float2)( MinMax.z - MinMax.x, MinMax.w - MinMax.y );
}

static float Range(float Value,float Start,float End)
{
	return (Value-Start) / (End-Start);
}


//	center of rect in uv
static float2 GetMinMaxAlignment(float4 MinMax,int2 Center)
{
	float Alignmentx = Range( Center.x, MinMax.x, MinMax.z );
	float Alignmenty = Range( Center.y, MinMax.y, MinMax.w );
	return (float2)(Alignmentx,Alignmenty);
}

static bool GetValidMinMax(float4* MinMax,int2 SampleCoord,float4 Sample)
{
	*MinMax = GetMinMax( SampleCoord, Sample );
	
	//	filter
	float2 RectSize = GetMinMaxSize(*MinMax);
	
	if ( RectSize.x < MinRectSize.x )
		return false;
	if ( RectSize.y < MinRectSize.y )
		return false;
	
	float2 RectAlignment = GetMinMaxAlignment(*MinMax,SampleCoord);
	if ( RectAlignment.x < MinAlignment.x )
		return false;
	if ( RectAlignment.x > MaxAlignment.x )
		return false;
	if ( RectAlignment.y < MinAlignment.y )
		return false;
	if ( RectAlignment.y > MaxAlignment.y )
		return false;
	
	return true;
}


static float4 texture2D(__read_only image2d_t Image,float2 uv)
{
	sampler_t Sampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
	
	return read_imagef( Image, Sampler, uv );
}





static void DrawLineHorz(int2 Start,int2 End,__write_only image2d_t Frag,float4 Colour)
{
	int y = Start.y;
	for ( int x=Start.x;	x<=End.x;	x++ )
	{
		write_imagef( Frag, (int2)(x,y), Colour );
	}
}

static void DrawLineVert(int2 Start,int2 End,__write_only image2d_t Frag,float4 Colour)
{
	int x = Start.x;
	for ( int y=Start.y;	y<=End.y;	y++ )
	{
		write_imagef( Frag, (int2)(x,y), Colour );
	}
}




__kernel void DrawRects(int OffsetX,int OffsetY,__read_only image2d_t rectfilter,__read_only image2d_t undistort,__write_only image2d_t Frag)
{
	int tx = get_global_id(0) + OffsetX;
	int ty = get_global_id(1) + OffsetY;
	int2 xy = (int2)( tx, ty );
	int2 wh = get_image_dim(rectfilter);
	
	//	copy the original for background. will get non-uniformly overwritten
	//	because of the paralell execution, but simpler setup
	{
		sampler_t CopySampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
		float4 CopyPixel = read_imagef( undistort, CopySampler, xy );
		write_imagef( Frag, xy, CopyPixel );
	}
	
	
	float4 rgba = (float4)( (float)xy.x/(float)wh.x, xy.y/(float)wh.y,1,1);
	
	sampler_t Sampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
	float4 Sample = read_imagef( rectfilter, Sampler, xy );

	float4 MinMax;
	if ( !GetValidMinMax( &MinMax, xy, Sample ) )
		return;
	
	
	int Minx = clamp( (int)MinMax.x, 0, wh.x-1 );
	int Miny = clamp( (int)MinMax.y, 0, wh.y-1 );
	int Maxx = clamp( (int)MinMax.z, 0, wh.x-1 );
	int Maxy = clamp( (int)MinMax.w, 0, wh.y-1 );
	int2 TopLeft = (int2)(Minx,Miny);
	int2 TopRight = (int2)(Maxx,Miny);
	int2 BottomLeft = (int2)(Minx,Maxy);
	int2 BottomRight = (int2)(Maxx,Maxy);
	
	DrawLineHorz( TopLeft, TopRight, Frag, rgba );
	DrawLineHorz( BottomLeft, BottomRight, Frag, rgba );
	DrawLineVert( TopLeft, BottomLeft, Frag, rgba );
	DrawLineVert( TopRight, BottomRight, Frag, rgba );
}

static bool MinMaxMerge(__global float4* a,float4 b,float NearEdgeDist,bool Merge)
{
	float4 Diff = (*a)-b;
	bool x1 = ( fabs(Diff.x) < NearEdgeDist );
	bool y1 = ( fabs(Diff.y) < NearEdgeDist );
	bool x2 = ( fabs(Diff.z) < NearEdgeDist );
	bool y2 = ( fabs(Diff.w) < NearEdgeDist );

	if ( x1 && y1 && x2 && y2 )
	{
		//	gr: i'm concerned this might corrupt with multithread access..
		if ( Merge )
		{
			a->x = min( a->x, b.x );
			a->y = min( a->y, b.y );
			a->z = max( a->z, b.z );
			a->w = max( a->w, b.w );
		}
		return true;
	}
	return false;
}

static float4 NormaliseMinMax(float4 MinMax,int2 Size)
{
	float2 Sizef = (float2)(Size.x,Size.y);
	MinMax.xy /= Sizef;
	MinMax.zw /= Sizef;
	return MinMax;
}

__kernel void GatherMinMaxs(int OffsetX,int OffsetY,__read_only image2d_t rectfilter,
							global float4*			Matches,
							global volatile int*	MatchesCount,
							int						MatchesMax,
							float					RectMergeMax
						  )
{
	int tx = get_global_id(0) + OffsetX;
	int ty = get_global_id(1) + OffsetY;
	int2 xy = (int2)( tx, ty );
	int2 wh = get_image_dim(rectfilter);
		
	sampler_t Sampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
	float4 Sample = read_imagef( rectfilter, Sampler, xy );
	
	float4 MinMax;
	if ( !GetValidMinMax( &MinMax, xy, Sample ) )
		return;
	
	//	crude merge
	for ( int i=0;	i<min(*MatchesCount,MatchesMax);	i++ )
	{
		if ( MinMaxMerge( &Matches[i], MinMax, RectMergeMax, true ) )
			return;
	}
	
	//	normalise
	//MinMax = NormaliseMinMax( MinMax, wh );
	
	TArray_float4 MatchArray = { Matches, MatchesCount, MatchesMax };
	PushArray_float4( MatchArray, &MinMax );
}




__kernel void DistortMinMax(int IndexOffset,global float4* Rects)
{
	int RectIndex = get_global_id(0) + IndexOffset;
	float4 Rect = Rects[RectIndex];
	
	//	redistort rect corners
	float2 TopLeft = Rect.xy;
	float2 TopRight = Rect.zy;
	float2 BottomLeft = Rect.xw;
	float2 BottomRight = Rect.zw;
	
	//	make it square agian
}

