#version 330

out vec4 finalColor;

in vec4 fragColor;
in vec3 fragPosition;
in vec3 fragNormal;

uniform vec4 colDiffuse;     // The tint passed when drawing

uniform vec3 lightPos;
uniform vec3 lightDir;
uniform vec3 ambientColor;
uniform float ambientStrength;

void main()
{
    finalColor = vec4(colDiffuse.rgb + ambientColor * ambientStrength, fragColor.a);
    finalColor *= ((max(dot(fragNormal, normalize(lightDir)), 0.0) * 0.5) + 0.5);
}

