in vec2 uv;
const float LineWidth = 0.005;

uniform sampler2D	Background;
#define LINE_COUNT	100
uniform vec4		Lines[LINE_COUNT];
const vec4 LineColour = vec4(1,0,1,1);


float TimeAlongLine2(vec2 Position,vec2 Start,vec2 End)
{
	vec2 Direction = End - Start;
	float DirectionLength = length(Direction);
	float Projection = dot( Position - Start, Direction) / (DirectionLength*DirectionLength);
	
	return Projection;
}


vec2 NearestToLine2(vec2 Position,vec2 Start,vec2 End)
{
	float Projection = TimeAlongLine2( Position, Start, End );
	
	//	past start
	Projection = max( 0, Projection );
	//	past end
	Projection = min( 1, Projection );
	
	//	is using lerp faster than
	//	Near = Start + (Direction * Projection);
	float2 Near = mix( Start, End, Projection );
	return Near;
}

float DistanceToLine2(vec2 Position,vec2 Start,vec2 End)
{
	vec2 Near = NearestToLine2( Position, Start, End );
	return length( Near - Position );
}


void main()
{
	float Distances[LINE_COUNT];

	float NearestDistance = 999;
	for ( int i=0;	i<LINE_COUNT;	i++)
	{
		vec4 Line = Lines[i];
		Distances[i] = DistanceToLine2( uv, Line.xy, Line.zw );
		NearestDistance = min( NearestDistance, Distances[i] );
	}

	if ( NearestDistance <= LineWidth )
	{
		gl_FragColor = LineColour;
	}
	else
	{
		gl_FragColor = texture( Background, uv );
	}
}
