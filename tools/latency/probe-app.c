// Vulkan X11 app: paints a frame counter as 32 black/white blocks, logs "counter present_ns" to a file.
// usage: probe-app <fps> <fifo|mailbox|immediate> <logfile>
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 1920
#define H 1080
#define BW 16
#define BH 16
#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS && r_ != VK_SUBOPTIMAL_KHR) { fprintf(stderr, "%s: %d\n", #x, r_); exit(1); } } while (0)

static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }

int main(int argc, char **argv)
{
	int fps = atoi(argv[1]);
	// A negative rate skips one frame every 100-400 ms: the gaps mark frames on the client.
	const int skip_frames = fps < 0;
	if (skip_frames) fps = -fps;
	uint64_t next_skip = now_ns() + 200000000ull;
	VkPresentModeKHR mode = !strcmp(argv[2], "fifo") ? VK_PRESENT_MODE_FIFO_KHR : !strcmp(argv[2], "mailbox") ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
	FILE *log = fopen(argv[3], "w");

	xcb_connection_t *c = xcb_connect(NULL, NULL);
	xcb_screen_t *scr = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	xcb_window_t win = xcb_generate_id(c);
	uint32_t mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
	xcb_create_window(c, XCB_COPY_FROM_PARENT, win, scr->root, 0, 0, W, H, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual, XCB_CW_EVENT_MASK, &mask);
	char inpath[512]; snprintf(inpath, sizeof inpath, "%s.input", argv[3]);
	FILE *inlog = fopen(inpath, "w");
	xcb_map_window(c, win);
	xcb_flush(c);

	const char *iext[] = { "VK_KHR_surface", "VK_KHR_xcb_surface" };
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2, .pApplicationName = "probe-app" };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = iext };
	VkInstance inst; CK(vkCreateInstance(&ici, NULL, &inst));
	VkXcbSurfaceCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, .connection = c, .window = win };
	VkSurfaceKHR surf; CK(vkCreateXcbSurfaceKHR(inst, &sci, NULL, &surf));

	uint32_t n = 8; VkPhysicalDevice pds[8]; CK(vkEnumeratePhysicalDevices(inst, &n, pds));
	VkPhysicalDevice pd = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < n && !pd; i++) {
		VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pds[i], &p);
		if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = pds[i];
	}
	if (!pd) pd = pds[0];

	float prio = 1;
	VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
	const char *dext[] = { "VK_KHR_swapchain" };
	VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci, .enabledExtensionCount = 1, .ppEnabledExtensionNames = dext };
	VkDevice dev; CK(vkCreateDevice(pd, &dci, NULL, &dev));
	VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);

	VkSurfaceCapabilitiesKHR caps; CK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps));
	uint32_t nf = 32; VkSurfaceFormatKHR fmts[32]; CK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, fmts));
	VkSurfaceFormatKHR fmt = fmts[0];
	for (uint32_t i = 0; i < nf; i++) if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) fmt = fmts[i];

	VkSwapchainCreateInfoKHR swci = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surf, .minImageCount = getenv("PROBE_IMAGES") ? (uint32_t)atoi(getenv("PROBE_IMAGES")) : (caps.minImageCount < 3 ? 3 : caps.minImageCount),
		.imageFormat = fmt.format, .imageColorSpace = fmt.colorSpace, .imageExtent = { W, H }, .imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = mode, .clipped = VK_FALSE };
	VkSwapchainKHR sw; CK(vkCreateSwapchainKHR(dev, &swci, NULL, &sw));
	uint32_t ni = 16; VkImage imgs[16]; CK(vkGetSwapchainImagesKHR(dev, sw, &ni, imgs));

	VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
	VkCommandPool pool; CK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
	VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
	VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));

	VkSemaphoreCreateInfo seci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkSemaphore acq, rend; CK(vkCreateSemaphore(dev, &seci, NULL, &acq)); CK(vkCreateSemaphore(dev, &seci, NULL, &rend));
	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
	VkFence done; CK(vkCreateFence(dev, &fci, NULL, &done));

	// With PROBE_NOISE set every frame is new noise: the encoder's worst case, frames at the VBV limit.
	const int noise = getenv("PROBE_NOISE") != NULL;
	// With PROBE_PAN set the frame pans over layered detail: encodable like a game's motion.
	const int pan = getenv("PROBE_PAN") != NULL;
	const VkDeviceSize frame_bytes = (VkDeviceSize)W * H * 4;
	VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 32 * BW * BH * 4 + (noise ? 16 * frame_bytes : 0) + (pan ? 4 * frame_bytes : 0), .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
	VkBuffer buf; CK(vkCreateBuffer(dev, &bci, NULL, &buf));
	VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, buf, &mr);
	VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	uint32_t mt = 0;
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((mr.memoryTypeBits >> i & 1) && (mp.memoryTypes[i].propertyFlags & 6) == 6) { mt = i; break; }
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = mt };
	VkDeviceMemory mem; CK(vkAllocateMemory(dev, &mai, NULL, &mem)); CK(vkBindBufferMemory(dev, buf, mem, 0));
	uint32_t *pix; CK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&pix));
	if (noise) {
		uint32_t x = 2463534242u, *n = pix + 32 * BW * BH;
		for (VkDeviceSize i = 0; i < 16 * frame_bytes / 4; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; n[i] = x | 0xff000000; }
	}

	if (pan) {
		// 3840x2160 of value noise, octaves from 256 px to 2 px, each a little fainter.
		const int PW = 3840, PH = 2160;
		float *f = calloc((size_t)PW * PH, sizeof(float));
		uint32_t x = 88172645u;
		float amp = 1.0f;
		for (int cell = 256; cell >= 2; cell /= 2, amp *= 0.72f) {
			const int gw = PW / cell + 2, gh = PH / cell + 2;
			float *g = malloc(sizeof(float) * gw * gh);
			for (int i = 0; i < gw * gh; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; g[i] = (x & 0xffff) / 65535.0f - 0.5f; }
			for (int py = 0; py < PH; py++) {
				int gy = py / cell; float ty = (float)(py % cell) / cell; ty = ty * ty * (3 - 2 * ty);
				for (int px = 0; px < PW; px++) {
					int gx = px / cell; float tx = (float)(px % cell) / cell; tx = tx * tx * (3 - 2 * tx);
					float a = g[gy * gw + gx] + (g[gy * gw + gx + 1] - g[gy * gw + gx]) * tx;
					float b = g[(gy + 1) * gw + gx] + (g[(gy + 1) * gw + gx + 1] - g[(gy + 1) * gw + gx]) * tx;
					f[py * PW + px] += amp * (a + (b - a) * ty);
				}
			}
			free(g);
		}
		uint32_t *o = pix + 32 * BW * BH + (noise ? 16 * frame_bytes / 4 : 0);
		for (int i = 0; i < PW * PH; i++) { int v = 128 + (int)(f[i] * 110); v = v < 0 ? 0 : v > 255 ? 255 : v; o[i] = 0xff000000u | v << 16 | (v * 3 / 4) << 8 | (255 - v) / 2; }
		free(f);
	}
	// With PROBE_LOAD=N each frame does N 4K filtered blits on the graphics queue: a GPU-bound game.
	const int load = getenv("PROBE_LOAD") ? atoi(getenv("PROBE_LOAD")) : 0;
	VkImage load_src = VK_NULL_HANDLE, load_dst = VK_NULL_HANDLE;
	if (load) {
		VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { 3840, 2160, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
		CK(vkCreateImage(dev, &ii, NULL, &load_src)); CK(vkCreateImage(dev, &ii, NULL, &load_dst));
		for (int k = 0; k < 2; k++) {
			VkImage im = k ? load_dst : load_src;
			VkMemoryRequirements ir; vkGetImageMemoryRequirements(dev, im, &ir);
			uint32_t t = 0;
			for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((ir.memoryTypeBits >> i & 1) && (mp.memoryTypes[i].propertyFlags & 1)) { t = i; break; }
			VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = ir.size, .memoryTypeIndex = t };
			VkDeviceMemory im_mem; CK(vkAllocateMemory(dev, &ai, NULL, &im_mem)); CK(vkBindImageMemory(dev, im, im_mem, 0));
		}
	}
	// PROBE_PERIOD_NS paces at an exact period, to match a panel that isn't quite at its nominal rate.
	const uint64_t period = getenv("PROBE_PERIOD_NS") ? strtoull(getenv("PROBE_PERIOD_NS"), NULL, 10) : 1000000000ull / fps;
	uint64_t next = now_ns() + period;
	for (uint32_t counter = 1;; counter++) {
		for (xcb_generic_event_t *e; (e = xcb_poll_for_event(c)); free(e)) {
			int t = e->response_type & 0x7f;
			if (t == XCB_KEY_PRESS || t == XCB_KEY_RELEASE || t == XCB_BUTTON_PRESS || t == XCB_BUTTON_RELEASE || t == XCB_MOTION_NOTIFY) {
				xcb_key_press_event_t *k = (void *)e;
				fprintf(inlog, "%llu type=%d detail=%d x=%d y=%d\n", (unsigned long long)now_ns(), t, k->detail, k->event_x, k->event_y);
				fflush(inlog);
			}
		}
		uint32_t idx;
		VkResult ar = vkAcquireNextImageKHR(dev, sw, UINT64_MAX, acq, VK_NULL_HANDLE, &idx);
		if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { fprintf(stderr, "acquire %d\n", ar); return 1; }
		CK(vkWaitForFences(dev, 1, &done, VK_TRUE, UINT64_MAX)); CK(vkResetFences(dev, 1, &done));

		VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
		CK(vkBeginCommandBuffer(cb, &bi));
		VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (load) {
			VkImageMemoryBarrier lb[2] = {
				{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .image = load_src, .subresourceRange = range, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED },
				{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .image = load_dst, .subresourceRange = range, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED } };
			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, lb);
			for (int i = 0; i < load; i++) {
				VkImageBlit r = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .srcOffsets = { { 0, 0, 0 }, { 3840, 2160, 1 } }, .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .dstOffsets = { { 3840, 2160, 0 }, { 0, 0, 1 } } };
				vkCmdBlitImage(cb, load_src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, load_dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r, VK_FILTER_LINEAR);
			}
		}
		VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .image = imgs[idx], .subresourceRange = range,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED };
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
		VkClearColorValue grey = { .float32 = { 0.25f, 0.25f, 0.25f, 1 } };
		vkCmdClearColorImage(cb, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &grey, 1, &range);
		for (int y = 0; y < BH; y++)
			for (int i = 0; i < 32; i++)
				for (int x = 0; x < BW; x++)
					pix[y * 32 * BW + i * BW + x] = (counter >> i) & 1 ? 0xffffffff : 0xff000000;
		VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { 32 * BW, BH, 1 } };
		if (noise) {
			VkBufferImageCopy whole = { .bufferOffset = 32 * BW * BH * 4 + (counter % 16) * frame_bytes, .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
			vkCmdCopyBufferToImage(cb, buf, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &whole);
		}
		if (pan) {
			const int still = getenv("PROBE_STILL") != NULL;
			// Back and forth: a wrap would be a scene cut, and those disturb what is being measured.
			const uint32_t xp = counter * 6 % 3840, yp = counter * 2 % 2160;
			const uint32_t ox = still ? 0 : xp < 1920 ? xp : 3840 - xp, oy = still ? 0 : yp < 1080 ? yp : 2160 - yp;
			VkBufferImageCopy whole = { .bufferOffset = 32 * BW * BH * 4 + (VkDeviceSize)(oy * 3840 + ox) * 4, .bufferRowLength = 3840, .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
			vkCmdCopyBufferToImage(cb, buf, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &whole);
		}
		vkCmdCopyBufferToImage(cb, buf, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = 0;
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b);
		CK(vkEndCommandBuffer(cb));
		VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acq, .pWaitDstStageMask = &ws,
			.commandBufferCount = 1, .pCommandBuffers = &cb, .signalSemaphoreCount = 1, .pSignalSemaphores = &rend };
		CK(vkQueueSubmit(q, 1, &si, done));
		VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &rend, .swapchainCount = 1, .pSwapchains = &sw, .pImageIndices = &idx };
		fprintf(log, "%u %llu\n", counter, (unsigned long long)now_ns());
		fflush(log);
		VkResult pr = vkQueuePresentKHR(q, &pi);
		if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) { fprintf(stderr, "present %d\n", pr); return 1; }
		uint64_t t = now_ns();
		if (t < next) { struct timespec d = { 0, (long)(next - t) }; nanosleep(&d, NULL); }
		next += period;
		if (skip_frames && now_ns() >= next_skip) {
			// Sparse markers keep the phase when self-paced: whole frames are missing.
			if (getenv("PROBE_SPARSE") && fps < 1000) {
				next += 5 * period;
			} else {
				// Long enough to show through the frames a FIFO swapchain has queued.
				struct timespec pause = { 0, getenv("PROBE_SPARSE") ? 100000000 : 34000000 };
				nanosleep(&pause, NULL);
				next = now_ns() + period;
			}
			next_skip = getenv("PROBE_SPARSE") ? now_ns() + 3000000000ull + (uint64_t)(rand() % 3000) * 1000000ull : now_ns() + 150000000ull + (uint64_t)(rand() % 350000000);
		}
		if (now_ns() > next + 4 * period) next = now_ns() + period;
	}
}
