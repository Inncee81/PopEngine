#define const	__constant

//const int SampleRadius = 8;	//	range 0,9
#define SampleRadius	5	//	range 0,9
const int HitCountMin = 2;
const bool IncludeSelf = true;


const float MinLum = 0.5f;
const float Tolerance = 0.01f;
const float AngleRange = 360.0f;


static float4 texture2D(__read_only image2d_t Image,int2 uv)
{
	sampler_t Sampler = CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;
	
	return read_imagef( Image, Sampler, uv );
}


float GetHslHslDifference(float3 a,float3 b)
{
	float ha = a.x;
	float hb = b.x;
	float sa = a.y;
	float sb = b.y;
	float la = a.z;
	float lb = b.z;
	
	float sdiff = fabs( sa - sb );
	float ldiff = fabs( la - lb );
	
	//	hue wraps, so difference needs to be calculated differently
	//	convert -1..1 to -0.5...0.5
	float hdiff = ha - hb;
	hdiff = ( hdiff > 0.5f ) ? hdiff - 1.f : hdiff;
	hdiff = ( hdiff < -0.5f ) ? hdiff + 1.f : hdiff;
	hdiff = fabs( hdiff );
	
	//	the higher the weight, the MORE difference it detects
	float hweight = 1.f;
	float sweight = 1.f;
	float lweight = 2.f;
#define NEAR_WHITE	0.8f
#define NEAR_BLACK	0.3f
#define NEAR_GREY	0.3f
	
	//	if a or b is too light, tone down the influence of hue and saturation
	{
		float l = max(la,lb);
		float Change = ( max(la,lb) > NEAR_WHITE ) ? ((l - NEAR_WHITE) / ( 1.f - NEAR_WHITE )) : 0.f;
		hweight *= 1.f - Change;
		sweight *= 1.f - Change;
	}
	//	else
	{
		float l = min(la,lb);
		float Change = ( min(la,lb) < NEAR_BLACK ) ? l / NEAR_BLACK : 1.f;
		hweight *= Change;
		sweight *= Change;
	}
	
	//	if a or b is undersaturated, we reduce weight of hue
	
	{
		float s = min(sa,sb);
		hweight *= ( min(sa,sb) < NEAR_GREY ) ? s / NEAR_GREY : 1.f;
	}
	
	
	//	normalise weights to 1.f
	float Weight = hweight + sweight + lweight;
	hweight /= Weight;
	sweight /= Weight;
	lweight /= Weight;
	
	float Diff = 0.f;
	Diff += hdiff * hweight;
	Diff += sdiff * sweight;
	Diff += ldiff * lweight;
	
	//	nonsense HSL values result in nonsense diff, so limit output
	Diff = min( Diff, 1.f );
	Diff = max( Diff, 0.f );
	return Diff;
}

int GetWalk(int2 xy,int2 WalkStep,__read_only image2d_t Image,int MaxSteps,float MaxDiff,bool* HitEdge)
{
	float3 BaseHsl = texture2D( Image, xy ).xyz;
	int2 wh = get_image_dim(Image);
	int2 Min = (int2)(0,0);
	int2 Max = (int2)(wh.x-1,wh.y-1);
	
	int Step = 0;
	for ( Step=0;	Step<=MaxSteps;	Step++ )
	{
		int2 Offset = WalkStep * (Step+1);
		int2 Matchxy = xy + Offset;
		if ( Matchxy.x < Min.x || Matchxy.y < Min.y || Matchxy.x > Max.x || Matchxy.y > Max.y )
		{
			*HitEdge = true;
			break;
		}
		
		float3 MatchHsl = texture2D( Image, Matchxy ).xyz;
		float Diff = GetHslHslDifference( BaseHsl, MatchHsl );
		if ( Diff > MaxDiff )
			break;
	}
	return Step;
}

__kernel void FilterColourPatch(int OffsetX,int OffsetY,__read_only image2d_t Hsl,__read_only image2d_t undistort,__write_only image2d_t Frag)
{
	int2 uv = (int2)( get_global_id(0) + OffsetX, get_global_id(1) + OffsetY );
	int2 wh = get_image_dim(Hsl);
	
	//	walk left & right until we hit a HSL edge
	int MaxDistance = 10;
	float MaxHslDiff = 0.045f;
	
	bool HitRightEdge = false;
	bool HitLeftEdge = false;
	int Right = GetWalk( uv, (int2)(1,0), Hsl, MaxDistance, MaxHslDiff, &HitRightEdge );

	HitRightEdge = (Right < MaxDistance);
	
	//	if we hit an image edge going left or right, then let the other direction go further
	int Left = GetWalk( uv, (int2)(-1,0), Hsl, MaxDistance + (HitRightEdge?MaxDistance-Right:0), MaxHslDiff, &HitLeftEdge );
	HitLeftEdge = (Left < MaxDistance);

	if ( HitLeftEdge )
	{
		//	re-calc right if we hit the left edge
		Right = GetWalk( uv, (int2)(1,0), Hsl, MaxDistance + (HitLeftEdge?MaxDistance-Left:0), MaxHslDiff, &HitRightEdge );
	}
	
	float Score = (float)(Left+Right) / (float)(MaxDistance+MaxDistance);
	int ScorePx = Left+Right;
	
	//float4 Rgba = (float4)( 0, Score, 0, 1 );
	float4 Rgba = texture2D( undistort, uv );
	
	//	erase if "this colour" has a width more than N
	if ( ScorePx > 10 )
		Rgba = (float4)(0,0,0,0);
	
	write_imagef( Frag, uv, Rgba );
}
