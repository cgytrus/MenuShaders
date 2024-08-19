const float PI = 3.14159265;

vec2 rotate(vec2 v, float a) {
	float sinA = sin(a);
	float cosA = cos(a);
	return vec2(v.x * cosA - v.y * sinA, v.y * cosA + v.x * sinA); 	
}

float square(vec2 uv, float d) {
	return max(abs(uv.x), abs(uv.y)) - d;	
}

float smootheststep(float edge0, float edge1, float x)
{
    x = clamp((x - edge0)/(edge1 - edge0), 0.0, 1.0) * 3.14159265;
    return 0.5 - (cos(x) * 0.5);
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = fragCoord.xy / iResolution.xy;
	uv = uv * 2.0 - 1.0;
	uv.x *= iResolution.x / iResolution.y;
	uv *= 1.5;
	
    float blurAmount = -0.005 * 1080.0 / iResolution.y;
    
	float period = 2.0;
	float time = iTime / period;
	time = mod(time, 1.0);
	time = smootheststep(0.0, 1.0, time);
	
	fragColor = vec4(0.0, 0.0, 0.0, 1.0);
	for (int i = 0; i < 9; i++) {
		float n = float(i);
		float size = 1.0 - n / 9.0;
		float rotateAmount .rgb, vec3(0.0), smoothstep(0.0, blurAmount, square(rotate(uv, -(rotateAmount + PI / 2.0) * time), size - blackOffset)));
    }
}
