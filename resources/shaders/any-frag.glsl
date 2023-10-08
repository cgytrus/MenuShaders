/*
Here's an extremely simple shader that just shows the uv,
while also showing all available uniforms

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;
uniform float pulse1; // i recommend this one
uniform float pulse2;
uniform float pulse3;
uniform float fft[744];

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    uv.x *= resolution.x / resolution.y; // optional: fix aspect ratio
    gl_FragColor = vec4(uv, 0.0, 1.0);
}
*/

// default shader is a blue swirly pattern
// which i made some time ago in shadertoy
// https://www.shadertoy.com/view/wdG3Wh

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse; // not used here

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(in vec2 p) {
    const float K1 = 0.366025404; // (sqrt(3) - 1) / 2;
    const float K2 = 0.211324865; // (3 - sqrt(3)) / 6;

    vec2 i = floor(p + (p.x + p.y) * K1);
    vec2 a = p - i + (i.x + i.y) * K2;
    float m = step(a.y, a.x);
    vec2 o = vec2(m, 1.0 - m);
    vec2 b = a - o + K2;
    vec2 c = a - 1.0 + 2.0 * K2;
    vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    vec3 n = h * h * h * h * vec3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));
    return dot(n, vec3(70.0));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    uv = vec2(noise(uv + time * 0.1), noise(uv + 10.0));
    float d = uv.x - uv.y;
    d *= 20.0;
    d = sin(d);
    d = d * 0.5 + 0.5;
    d = 1.0 - d;

    d = smoothstep(0.1, 0.1, d);

    vec3 col = vec3(mix(vec3(0.1), vec3(0.2, 0.2, 0.6), d));

    gl_FragColor = vec4(col, 1.0);
}
