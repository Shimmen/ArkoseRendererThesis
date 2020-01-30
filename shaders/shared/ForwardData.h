#ifndef FORWARD_DATA_H
#define FORWARD_DATA_H

#define FORWARD_MAX_TRANSFORMS 128

// vkCreatePipelineLayout(): max per-stage sampler bindings count (64) exceeds device maxPerStageDescriptorSamplers limit (16).
// The Vulkan spec states: The total number of descriptors of the type VK_DESCRIPTOR_TYPE_SAMPLER and VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
// accessible to any shader stage across all elements of pSetLayouts must be less than or equal to VkPhysicalDeviceLimits::maxPerStageDescriptorSampler
// (https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#VUID-VkPipelineLayoutCreateInfo-pSetLayouts-00287)
// TODO: Consider if we should use something else, like separate images & samplers, or if we should just
//  require fancier computers for when you want to load fancier models with more textures. On the other hand,
//  16 is nothing. Barley the most basic model with PBR materials coudl go through here as is is now...
#define FORWARD_MAX_TEXTURES 16

struct ForwardMaterial {
    int samplerIndex;
};

#endif // FORWARD_DATA_H
