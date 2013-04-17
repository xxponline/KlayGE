// DeferredRenderingLayer.cpp
// KlayGE Deferred Rendering Layer implement file
// Ver 4.0.0
// Copyright(C) Minmin Gong, 2011
// Homepage: http://www.klayge.org
//
// 4.0.0
// First release (2011.8.28)
//
// CHANGE LIST
//////////////////////////////////////////////////////////////////////////////////

#include <KlayGE/KlayGE.hpp>
#include <KFL/Util.hpp>
#include <KFL/Math.hpp>
#include <KlayGE/ResLoader.hpp>
#include <KlayGE/Renderable.hpp>
#include <KlayGE/RenderableHelper.hpp>
#include <KlayGE/RenderEngine.hpp>
#include <KlayGE/RenderEffect.hpp>
#include <KlayGE/FrameBuffer.hpp>
#include <KlayGE/Context.hpp>
#include <KlayGE/SceneObjectHelper.hpp>
#include <KlayGE/SceneManager.hpp>
#include <KlayGE/RenderFactory.hpp>
#include <KlayGE/App3D.hpp>
#include <KlayGE/PostProcess.hpp>
#include <KlayGE/Query.hpp>
#include <KlayGE/Camera.hpp>
#include <KlayGE/Mesh.hpp>
#include <KlayGE/SSVOPostProcess.hpp>
#include <KlayGE/SSRPostProcess.hpp>

#include <KlayGE/DeferredRenderingLayer.hpp>

#define USE_NEW_LIGHT_SAMPLING

namespace KlayGE
{
	int const SM_SIZE = 512;
	
	int const VPL_COUNT_SQRT = 16;
	
	float const VPL_DELTA = 1.0f / VPL_COUNT_SQRT;
	float const VPL_OFFSET = 0.5f * VPL_DELTA;

	int const MAX_IL_MIPMAP_LEVELS = 3;

#ifdef USE_NEW_LIGHT_SAMPLING
	int const MIN_RSM_MIPMAP_SIZE = 8; // minimum mipmap size is 8x8
	int const MAX_RSM_MIPMAP_LEVELS = 7; // (log(512)-log(4))/log(2) + 1
	int const BEGIN_RSM_SAMPLING_LIGHT_LEVEL = 5;
	int const SAMPLE_LEVEL_CNT = MAX_RSM_MIPMAP_LEVELS - BEGIN_RSM_SAMPLING_LIGHT_LEVEL;
	int const VPL_COUNT = 64 * ((1UL << (SAMPLE_LEVEL_CNT * 2)) - 1) / (4 - 1);
#else
	int const MAX_RSM_MIPMAP_LEVELS = 6;
	int const VPL_COUNT = VPL_COUNT_SQRT * VPL_COUNT_SQRT;
#endif

	template <typename T>
	void CreateConeMesh(std::vector<T>& vb, std::vector<uint16_t>& ib, uint16_t vertex_base, float radius, float height, uint16_t n)
	{
		for (int i = 0; i < n; ++ i)
		{
			vb.push_back(T());
			vb.back().x() = vb.back().y() = vb.back().z() = 0;
		}

		float outer_radius = radius / cos(PI / n);
		for (int i = 0; i < n; ++ i)
		{
			vb.push_back(T());
			float angle = i * 2 * PI / n;
			vb.back().x() = outer_radius * cos(angle);
			vb.back().y() = outer_radius * sin(angle);
			vb.back().z() = height;
		}

		vb.push_back(T());
		vb.back().x() = vb.back().y() = 0;
		vb.back().z() = height;

		for (int i = 0; i < n; ++ i)
		{
			vb.push_back(T());
			vb.back() = vb[vertex_base + n + i];
		}

		for (uint16_t i = 0; i < n - 1; ++ i)
		{
			ib.push_back(vertex_base + i);
			ib.push_back(vertex_base + n + i + 1);
			ib.push_back(vertex_base + n + i);
		}
		ib.push_back(vertex_base + n - 1);
		ib.push_back(vertex_base + n + 0);
		ib.push_back(vertex_base + n + n - 1);

		for (uint16_t i = 0; i < n - 1; ++ i)
		{
			ib.push_back(vertex_base + 2 * n);
			ib.push_back(vertex_base + 2 * n + 1 + i);
			ib.push_back(vertex_base + 2 * n + 1 + i + 1);
		}
		ib.push_back(vertex_base + 2 * n);
		ib.push_back(vertex_base + 2 * n + 1 + n - 1);
		ib.push_back(vertex_base + 2 * n + 1);
	}

	template <typename T>
	void CreatePyramidMesh(std::vector<T>& vb, std::vector<uint16_t>& ib, uint16_t vertex_base, float radius, float height)
	{
		for (int i = 0; i < 4; ++ i)
		{
			vb.push_back(T());
			vb.back().x() = vb.back().y() = vb.back().z() = 0;
		}

		float outer_radius = radius * sqrt(2.0f);
		vb.push_back(T());
		vb.back().x() = -outer_radius;
		vb.back().y() = -outer_radius;
		vb.back().z() = height;
		vb.push_back(T());
		vb.back().x() = +outer_radius;
		vb.back().y() = -outer_radius;
		vb.back().z() = height;
		vb.push_back(T());
		vb.back().x() = +outer_radius;
		vb.back().y() = +outer_radius;
		vb.back().z() = height;
		vb.push_back(T());
		vb.back().x() = -outer_radius;
		vb.back().y() = +outer_radius;
		vb.back().z() = height;

		vb.push_back(T());
		vb.back().x() = vb.back().y() = 0;
		vb.back().z() = height;

		for (int i = 0; i < 4; ++ i)
		{
			vb.push_back(T());
			vb.back() = vb[vertex_base + 4 + i];
		}

		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 5);
		ib.push_back(vertex_base + 4);
		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 5);
		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 7);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 4);
		ib.push_back(vertex_base + 7);

		ib.push_back(vertex_base + 8);
		ib.push_back(vertex_base + 9);
		ib.push_back(vertex_base + 10);
		ib.push_back(vertex_base + 8);
		ib.push_back(vertex_base + 10);
		ib.push_back(vertex_base + 11);
		ib.push_back(vertex_base + 8);
		ib.push_back(vertex_base + 11);
		ib.push_back(vertex_base + 12);
		ib.push_back(vertex_base + 8);
		ib.push_back(vertex_base + 12);
		ib.push_back(vertex_base + 9);
	}

	template <typename T>
	void CreateBoxMesh(std::vector<T>& vb, std::vector<uint16_t>& ib, uint16_t vertex_base, float half_length)
	{
		vb.push_back(T());
		vb.back().x() = -half_length;
		vb.back().y() = +half_length;
		vb.back().z() = -half_length;
		vb.push_back(T());
		vb.back().x() = +half_length;
		vb.back().y() = +half_length;
		vb.back().z() = -half_length;
		vb.push_back(T());
		vb.back().x() = +half_length;
		vb.back().y() = -half_length;
		vb.back().z() = -half_length;
		vb.push_back(T());
		vb.back().x() = -half_length;
		vb.back().y() = -half_length;
		vb.back().z() = -half_length;

		vb.push_back(T());
		vb.back().x() = -half_length;
		vb.back().y() = +half_length;
		vb.back().z() = +half_length;
		vb.push_back(T());
		vb.back().x() = +half_length;
		vb.back().y() = +half_length;
		vb.back().z() = +half_length;
		vb.push_back(T());
		vb.back().x() = +half_length;
		vb.back().y() = -half_length;
		vb.back().z() = +half_length;
		vb.push_back(T());
		vb.back().x() = -half_length;
		vb.back().y() = -half_length;
		vb.back().z() = +half_length;

		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 1);
		ib.push_back(vertex_base + 2);
		ib.push_back(vertex_base + 2);
		ib.push_back(vertex_base + 3);
		ib.push_back(vertex_base + 0);

		ib.push_back(vertex_base + 5);
		ib.push_back(vertex_base + 4);
		ib.push_back(vertex_base + 7);
		ib.push_back(vertex_base + 7);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 5);

		ib.push_back(vertex_base + 4);
		ib.push_back(vertex_base + 5);
		ib.push_back(vertex_base + 1);
		ib.push_back(vertex_base + 1);
		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 4);

		ib.push_back(vertex_base + 1);
		ib.push_back(vertex_base + 5);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 2);
		ib.push_back(vertex_base + 1);

		ib.push_back(vertex_base + 3);
		ib.push_back(vertex_base + 2);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 6);
		ib.push_back(vertex_base + 7);
		ib.push_back(vertex_base + 3);

		ib.push_back(vertex_base + 4);
		ib.push_back(vertex_base + 0);
		ib.push_back(vertex_base + 3);
		ib.push_back(vertex_base + 3);
		ib.push_back(vertex_base + 7);
		ib.push_back(vertex_base + 4);
	}


	DeferredRenderingLayer::DeferredRenderingLayer()
		: active_viewport_(0), ssr_enabled_(true), taa_enabled_(true),
			light_scale_(1), illum_(0), indirect_scale_(1.0f)
	{
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderEngine& re = rf.RenderEngineInstance();
		RenderDeviceCaps const & caps = re.DeviceCaps();

		ElementFormat ds_fmt;
		if (caps.texture_format_support(EF_D24S8))
		{
			ds_fmt = EF_D24S8;
			depth_texture_support_ = true;
		}
		else
		{
			if (caps.texture_format_support(EF_D16))
			{				
				ds_fmt = EF_D16;
				depth_texture_support_ = true;
			}
			else
			{
				ds_fmt = EF_Unknown;
				depth_texture_support_ = false;
			}
		}

		mrt_g_buffer_support_ = (caps.max_simultaneous_rts > 1);

		for (size_t vpi = 0; vpi < viewports_.size(); ++ vpi)
		{
			PerViewport& pvp = viewports_[vpi];
			for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
			{
				if (!depth_texture_support_)
				{
					pvp.pre_depth_buffers[i] = rf.MakeFrameBuffer();
				}
				pvp.g_buffers[i] = rf.MakeFrameBuffer();
				if (!mrt_g_buffer_support_)
				{
					pvp.g_buffers_rt1[i] = rf.MakeFrameBuffer();
				}
				pvp.lighting_buffers[i] = rf.MakeFrameBuffer();
				pvp.shading_buffers[i] = rf.MakeFrameBuffer();
				pvp.curr_merged_shading_buffers[i] = rf.MakeFrameBuffer();
				pvp.curr_merged_depth_buffers[i] = rf.MakeFrameBuffer();
				pvp.prev_merged_shading_buffers[i] = rf.MakeFrameBuffer();
				pvp.prev_merged_depth_buffers[i] = rf.MakeFrameBuffer();
			}
			pvp.shadowing_buffer = rf.MakeFrameBuffer();
		}

		{
			rl_cone_ = rf.MakeRenderLayout();
			rl_cone_->TopologyType(RenderLayout::TT_TriangleList);

			std::vector<float3> pos;
			std::vector<uint16_t> index;
			CreateConeMesh(pos, index, 0, 100.0f, 100.0f, 12);
			cone_obb_ = MathLib::compute_obbox(pos.begin(), pos.end());

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(pos[0]));
			init_data.slice_pitch = 0;
			init_data.data = &pos[0];
			rl_cone_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data),
				make_tuple(vertex_element(VEU_Position, 0, EF_BGR32F)));

			init_data.row_pitch = static_cast<uint32_t>(index.size() * sizeof(index[0]));
			init_data.data = &index[0];
			rl_cone_->BindIndexStream(rf.MakeIndexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data), EF_R16UI);
		}
		{
			rl_pyramid_ = rf.MakeRenderLayout();
			rl_pyramid_->TopologyType(RenderLayout::TT_TriangleList);

			std::vector<float3> pos;
			std::vector<uint16_t> index;
			CreatePyramidMesh(pos, index, 0, 100.0f, 100.0f);
			pyramid_obb_ = MathLib::compute_obbox(pos.begin(), pos.end());

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(pos[0]));
			init_data.slice_pitch = 0;
			init_data.data = &pos[0];
			rl_pyramid_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data),
				make_tuple(vertex_element(VEU_Position, 0, EF_BGR32F)));

			init_data.row_pitch = static_cast<uint32_t>(index.size() * sizeof(index[0]));
			init_data.data = &index[0];
			rl_pyramid_->BindIndexStream(rf.MakeIndexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data), EF_R16UI);
		}
		{
			rl_box_ = rf.MakeRenderLayout();
			rl_box_->TopologyType(RenderLayout::TT_TriangleList);

			std::vector<float3> pos;
			std::vector<uint16_t> index;
			CreateBoxMesh(pos, index, 0, 100.0f);
			box_obb_ = MathLib::compute_obbox(pos.begin(), pos.end());

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(pos[0]));
			init_data.slice_pitch = 0;
			init_data.data = &pos[0];
			rl_box_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data),
				make_tuple(vertex_element(VEU_Position, 0, EF_BGR32F)));

			init_data.row_pitch = static_cast<uint32_t>(index.size() * sizeof(index[0]));
			init_data.data = &index[0];
			rl_box_->BindIndexStream(rf.MakeIndexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data), EF_R16UI);
		}
		{
			rl_quad_ = rf.MakeRenderLayout();
			rl_quad_->TopologyType(RenderLayout::TT_TriangleStrip);

			std::vector<float3> pos;
			std::vector<uint16_t> index;

			pos.push_back(float3(+1, +1, 1));
			pos.push_back(float3(-1, +1, 1));
			pos.push_back(float3(+1, -1, 1));
			pos.push_back(float3(-1, -1, 1));

			ElementInitData init_data;
			init_data.row_pitch = static_cast<uint32_t>(pos.size() * sizeof(pos[0]));
			init_data.slice_pitch = 0;
			init_data.data = &pos[0];
			rl_quad_->BindVertexStream(rf.MakeVertexBuffer(BU_Static, EAH_GPU_Read | EAH_Immutable, &init_data),
				make_tuple(vertex_element(VEU_Position, 0, EF_BGR32F)));
		}

		g_buffer_effect_ = SyncLoadRenderEffect("GBuffer.fxml");
		dr_effect_ = SyncLoadRenderEffect("DeferredRendering.fxml");

		technique_shadows_[LT_Ambient] = dr_effect_->TechniqueByName("DeferredShadowingAmbient");
		technique_shadows_[LT_Directional] = dr_effect_->TechniqueByName("DeferredShadowingDirectional");
		technique_shadows_[LT_Point] = dr_effect_->TechniqueByName("DeferredShadowingPoint");
		technique_shadows_[LT_Spot] = dr_effect_->TechniqueByName("DeferredShadowingSpot");
		technique_lights_[LT_Ambient] = dr_effect_->TechniqueByName("DeferredRenderingAmbient");
		technique_lights_[LT_Directional] = dr_effect_->TechniqueByName("DeferredRenderingDirectional");
		technique_lights_[LT_Point] = dr_effect_->TechniqueByName("DeferredRenderingPoint");
		technique_lights_[LT_Spot] = dr_effect_->TechniqueByName("DeferredRenderingSpot");
		technique_light_depth_only_ = dr_effect_->TechniqueByName("DeferredRenderingLightDepthOnly");
		technique_light_stencil_ = dr_effect_->TechniqueByName("DeferredRenderingLightStencil");
		technique_clear_stencil_ = dr_effect_->TechniqueByName("ClearStencil");
		technique_no_lighting_ = dr_effect_->TechniqueByName("NoLightingTech");
		technique_shading_ = dr_effect_->TechniqueByName("ShadingTech");
		technique_merge_shadings_[0] = dr_effect_->TechniqueByName("MergeShadingTech");
		technique_merge_shadings_[1] = dr_effect_->TechniqueByName("MergeShadingAlphaBlendTech");
		technique_merge_depths_[0] = dr_effect_->TechniqueByName("MergeDepthTech");
		technique_merge_depths_[1] = dr_effect_->TechniqueByName("MergeDepthAlphaBlendTech");
		technique_copy_shading_depth_ = dr_effect_->TechniqueByName("CopyShadingDepthTech");
		technique_copy_depth_ = dr_effect_->TechniqueByName("CopyDepthTech");

		sm_buffer_ = rf.MakeFrameBuffer();
		ElementFormat fmt;
		if (caps.rendertarget_format_support(EF_GR32F, 1, 0))
		{
			fmt = EF_GR32F;
		}
		else if (caps.rendertarget_format_support(EF_ABGR32F, 1, 0))
		{
			fmt = EF_ABGR32F;
		}
		else if (caps.rendertarget_format_support(EF_GR16F, 1, 0))
		{
			fmt = EF_GR16F;
		}
		else
		{
			BOOST_ASSERT(caps.rendertarget_format_support(EF_ABGR16F, 1, 0));

			fmt = EF_ABGR16F;
		}
		sm_tex_ = rf.MakeTexture2D(SM_SIZE, SM_SIZE, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		sm_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*sm_tex_, 0, 1, 0));
		RenderViewPtr sm_depth_view;
		if (depth_texture_support_)
		{
			sm_depth_tex_ = rf.MakeTexture2D(SM_SIZE, SM_SIZE, 1, 1, ds_fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
			sm_depth_view = rf.Make2DDepthStencilRenderView(*sm_depth_tex_, 0, 1, 0);
		}
		else
		{
			sm_depth_view = rf.Make2DDepthStencilRenderView(SM_SIZE, SM_SIZE, EF_D16, 1, 0);
		}
		sm_buffer_->Attach(FrameBuffer::ATT_DepthStencil, sm_depth_view);

		for (uint32_t i = 0; i < NUM_SHADOWED_SPOT_LIGHTS; ++ i)
		{
			blur_sm_2d_texs_[i] = rf.MakeTexture2D(SM_SIZE, SM_SIZE, 1, 1, sm_tex_->Format(), 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		}
		for (uint32_t i = 0; i < NUM_SHADOWED_POINT_LIGHTS; ++ i)
		{
			blur_sm_cube_texs_[i] = rf.MakeTextureCube(SM_SIZE, 1, 1, sm_tex_->Format(), 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		}

		ssvo_pp_ = MakeSharedPtr<SSVOPostProcess>();
		ssvo_blur_pp_ = MakeSharedPtr<BlurPostProcess<SeparableBilateralFilterPostProcess> >(8, 1.0f,
			SyncLoadRenderEffect("SSVO.fxml")->TechniqueByName("SSVOBlurX"),
			SyncLoadRenderEffect("SSVO.fxml")->TechniqueByName("SSVOBlurY"));

		ssr_pp_ = MakeSharedPtr<SSRPostProcess>();

		taa_pp_ = SyncLoadPostProcess("TAA.ppml", "taa");

		if (mrt_g_buffer_support_)
		{
			rsm_buffer_ = rf.MakeFrameBuffer();

			ElementFormat fmt8;
			if (caps.rendertarget_format_support(EF_ABGR8, 1, 0))
			{
				fmt8 = EF_ABGR8;
			}
			else
			{
				BOOST_ASSERT(caps.rendertarget_format_support(EF_ARGB8, 1, 0));

				fmt8 = EF_ARGB8;
			}

			rsm_texs_[0] = rf.MakeTexture2D(SM_SIZE, SM_SIZE, MAX_RSM_MIPMAP_LEVELS, 1, fmt8, 1, 0, EAH_GPU_Read | EAH_GPU_Write | EAH_Generate_Mips, nullptr);
			rsm_texs_[1] = rf.MakeTexture2D(SM_SIZE, SM_SIZE, MAX_RSM_MIPMAP_LEVELS, 1, fmt8, 1, 0, EAH_GPU_Read | EAH_GPU_Write | EAH_Generate_Mips, nullptr);
			rsm_buffer_->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*rsm_texs_[0], 0, 1, 0)); // albedo
			rsm_buffer_->Attach(FrameBuffer::ATT_Color1, rf.Make2DRenderView(*rsm_texs_[1], 0, 1, 0)); // normal (light space)
			rsm_buffer_->Attach(FrameBuffer::ATT_DepthStencil, sm_depth_view);
			
			copy_to_light_buffer_pp_ = SyncLoadPostProcess("Copy2LightBuffer.ppml", "CopyToLightBuffer");
			copy_to_light_buffer_i_pp_ = SyncLoadPostProcess("Copy2LightBuffer.ppml", "CopyToLightBufferI");
		}


		sm_filter_pp_ = MakeSharedPtr<BlurPostProcess<SeparableGaussianFilterPostProcess> >(8, 1.0f);
		if (depth_texture_support_)
		{
			depth_to_vsm_pp_ = SyncLoadPostProcess("DepthToSM.ppml", "DepthToVSM");
			depth_to_vsm_pp_->InputPin(0, sm_depth_tex_);
			depth_to_vsm_pp_->OutputPin(0, sm_tex_);

			depth_to_linear_pp_ = SyncLoadPostProcess("DepthToSM.ppml", "DepthToSM");
		}

		g_buffer_tex_param_ = dr_effect_->ParameterByName("g_buffer_tex");
		g_buffer_1_tex_param_ = dr_effect_->ParameterByName("g_buffer_1_tex");
		depth_tex_param_ = dr_effect_->ParameterByName("depth_tex");
		lighting_tex_param_ = dr_effect_->ParameterByName("lighting_tex");
		shading_tex_param_ = dr_effect_->ParameterByName("shading_tex");
		depth_near_far_invfar_param_ = dr_effect_->ParameterByName("depth_near_far_invfar");
		light_attrib_param_ = dr_effect_->ParameterByName("light_attrib");
		light_color_param_ = dr_effect_->ParameterByName("light_color");
		light_falloff_param_ = dr_effect_->ParameterByName("light_falloff");
		light_view_proj_param_ = dr_effect_->ParameterByName("light_view_proj");
		light_volume_mv_param_ = dr_effect_->ParameterByName("light_volume_mv");
		light_volume_mvp_param_ = dr_effect_->ParameterByName("light_volume_mvp");
		view_to_light_model_param_ = dr_effect_->ParameterByName("view_to_light_model");
		light_pos_es_param_ = dr_effect_->ParameterByName("light_pos_es");
		light_dir_es_param_ = dr_effect_->ParameterByName("light_dir_es");
		projective_map_2d_tex_param_ = dr_effect_->ParameterByName("projective_map_2d_tex");
		projective_map_cube_tex_param_ = dr_effect_->ParameterByName("projective_map_cube_tex");
		shadow_map_2d_tex_param_ = dr_effect_->ParameterByName("shadow_map_2d_tex");
		shadow_map_cube_tex_param_ = dr_effect_->ParameterByName("shadow_map_cube_tex");
		inv_width_height_param_ = dr_effect_->ParameterByName("inv_width_height");
		shadowing_tex_param_ = dr_effect_->ParameterByName("shadowing_tex");
		near_q_param_ = dr_effect_->ParameterByName("near_q");
	}

	void DeferredRenderingLayer::SSGIEnabled(uint32_t vp, bool ssgi)
	{
		viewports_[vp].ssgi_enable = ssgi;
		this->SetupViewportGI(vp);
	}

	void DeferredRenderingLayer::SSVOEnabled(uint32_t vp, bool ssvo)
	{
		viewports_[vp].ssvo_enabled = ssvo;
	}

	void DeferredRenderingLayer::SSREnabled(bool ssr)
	{
		ssr_enabled_ = ssr;
	}

	void DeferredRenderingLayer::TemporalAAEnabled(bool taa)
	{
		taa_enabled_ = taa;
	}

	void DeferredRenderingLayer::AddDecal(RenderDecalPtr const & decal)
	{
		decals_.push_back(decal);
	}

	void DeferredRenderingLayer::SetupViewport(uint32_t index, FrameBufferPtr const & fb, uint32_t attrib)
	{
		PerViewport& pvp = viewports_[index];
		pvp.attrib = attrib;
		pvp.frame_buffer = fb;
		pvp.frame_buffer->GetViewport()->camera->JitterMode(true);

		if (fb)
		{
			pvp.attrib |= VPAM_Enabled;
		}

		uint32_t const width = pvp.frame_buffer->GetViewport()->width;
		uint32_t const height = pvp.frame_buffer->GetViewport()->height;

		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderDeviceCaps const & caps = rf.RenderEngineInstance().DeviceCaps();

		ElementFormat fmt8;
		if (caps.rendertarget_format_support(EF_ABGR8, 1, 0))
		{
			fmt8 = EF_ABGR8;
		}
		else
		{
			BOOST_ASSERT(caps.rendertarget_format_support(EF_ARGB8, 1, 0));

			fmt8 = EF_ARGB8;
		}
		ElementFormat depth_fmt;
		if (caps.rendertarget_format_support(EF_R16F, 1, 0))
		{
			depth_fmt = EF_R16F;
		}
		else
		{
			if (caps.rendertarget_format_support(EF_R32F, 1, 0))
			{
				depth_fmt = EF_R32F;
			}
			else
			{
				BOOST_ASSERT(caps.rendertarget_format_support(EF_ABGR16F, 1, 0));

				depth_fmt = EF_ABGR16F;
			}
		}

		std::vector<RenderViewPtr> ds_views(pvp.g_buffers.size());
		if (depth_texture_support_)
		{
			ElementFormat ds_fmt;
			if (caps.texture_format_support(EF_D24S8))
			{
				ds_fmt = EF_D24S8;
			}
			else
			{
				BOOST_ASSERT(caps.texture_format_support(EF_D16));
				
				ds_fmt = EF_D16;
			}

			for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
			{
				pvp.g_buffer_ds_texs[i] = rf.MakeTexture2D(width, height, 1, 1, ds_fmt, 1, 0,  EAH_GPU_Read | EAH_GPU_Write, nullptr);
				ds_views[i] = rf.Make2DDepthStencilRenderView(*pvp.g_buffer_ds_texs[i], 0, 0, 0);
			}
		}
		else
		{
			for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
			{
				ds_views[i] = rf.Make2DDepthStencilRenderView(width, height, EF_D16, 1, 0);
			}
		}

		for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
		{
			pvp.g_buffer_depth_texs[i] = rf.MakeTexture2D(width, height, MAX_IL_MIPMAP_LEVELS + 1, 1, depth_fmt, 1, 0,  EAH_GPU_Read | EAH_GPU_Write | EAH_Generate_Mips, nullptr);

			pvp.g_buffer_rt0_texs[i] = rf.MakeTexture2D(width, height, MAX_IL_MIPMAP_LEVELS + 1, 1, fmt8, 1, 0, EAH_GPU_Read | EAH_GPU_Write | EAH_Generate_Mips, nullptr);
			pvp.g_buffer_rt1_texs[i] = rf.MakeTexture2D(width, height, 1, 1, fmt8, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
			pvp.g_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.g_buffer_rt0_texs[i], 0, 1, 0));
			pvp.g_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
			if (mrt_g_buffer_support_)
			{
				pvp.g_buffers[i]->Attach(FrameBuffer::ATT_Color1, rf.Make2DRenderView(*pvp.g_buffer_rt1_texs[i], 0, 1, 0));
			}
			else
			{
				pvp.g_buffers_rt1[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.g_buffer_rt1_texs[i], 0, 1, 0));
				pvp.g_buffers_rt1[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
			}

			if (!depth_texture_support_)
			{
				pvp.pre_depth_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.g_buffer_depth_texs[i], 0, 1, 0));
				pvp.pre_depth_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
			}
		}

		this->SetupViewportGI(index);

		ElementFormat fmt;
		if (caps.rendertarget_format_support(EF_B10G11R11F, 1, 0))
		{
			fmt = EF_B10G11R11F;
		}
		else
		{
			if (caps.rendertarget_format_support(EF_ABGR8, 1, 0))
			{
				fmt = EF_ABGR8;
			}
			else
			{
				BOOST_ASSERT(caps.rendertarget_format_support(EF_ARGB8, 1, 0));

				fmt = EF_ARGB8;
			}
		}
		pvp.shadowing_tex = rf.MakeTexture2D(width / 2, height / 2, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		pvp.shadowing_buffer->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.shadowing_tex, 0, 1, 0));

		for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
		{
			pvp.lighting_texs[i] = rf.MakeTexture2D(width, height, 1, 1, EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
			pvp.lighting_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.lighting_texs[i], 0, 1, 0));
			pvp.lighting_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
		}

		if (caps.rendertarget_format_support(EF_B10G11R11F, 1, 0))
		{
			fmt = EF_B10G11R11F;
		}
		else
		{
			BOOST_ASSERT(caps.rendertarget_format_support(EF_ABGR16F, 1, 0));

			fmt = EF_ABGR16F;
		}
		pvp.curr_merged_shading_tex = rf.MakeTexture2D(width, height, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		pvp.curr_merged_depth_tex = rf.MakeTexture2D(width, height, 1, 1, depth_fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		pvp.prev_merged_shading_tex = rf.MakeTexture2D(width, height, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		pvp.prev_merged_depth_tex = rf.MakeTexture2D(width, height, 1, 1, depth_fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
		for (size_t i = 0; i < pvp.shading_buffers.size(); ++ i)
		{
			pvp.shading_texs[i] = rf.MakeTexture2D(width, height, 1, 1, (0 == i) ? fmt : EF_ABGR16F, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
			pvp.shading_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.shading_texs[i], 0, 1, 0));
			pvp.shading_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);

			pvp.curr_merged_shading_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.curr_merged_shading_tex, 0, 1, 0));
			pvp.curr_merged_shading_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
			pvp.curr_merged_depth_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.curr_merged_depth_tex, 0, 1, 0));
			pvp.curr_merged_depth_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);

			pvp.prev_merged_shading_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.prev_merged_shading_tex, 0, 1, 0));
			pvp.prev_merged_shading_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
			pvp.prev_merged_depth_buffers[i]->Attach(FrameBuffer::ATT_Color0, rf.Make2DRenderView(*pvp.prev_merged_depth_tex, 0, 1, 0));
			pvp.prev_merged_depth_buffers[i]->Attach(FrameBuffer::ATT_DepthStencil, ds_views[i]);
		}

		if (caps.rendertarget_format_support(EF_R8, 1, 0))
		{
			fmt = EF_R8;
		}
		else if (caps.rendertarget_format_support(EF_R16F, 1, 0))
		{
			fmt = EF_R16F;
		}
		else
		{
			BOOST_ASSERT(caps.rendertarget_format_support(EF_ABGR16F, 1, 0));

			fmt = EF_ABGR16F;
		}
		pvp.small_ssvo_tex = rf.MakeTexture2D(width / 2, height / 2, 1, 1, fmt, 1, 0, EAH_GPU_Read | EAH_GPU_Write, nullptr);
	}

	void DeferredRenderingLayer::EnableViewport(uint32_t index, bool enable)
	{
		PerViewport& pvp = viewports_[index];
		if (enable)
		{
			pvp.attrib |= VPAM_Enabled;
		}
		else
		{
			pvp.attrib &= ~VPAM_Enabled;
		}
	}

	uint32_t DeferredRenderingLayer::Update(uint32_t pass)
	{
		SceneManager& scene_mgr = Context::Instance().SceneManagerInstance();
		RenderFactory& rf = Context::Instance().RenderFactoryInstance();
		RenderEngine& re = rf.RenderEngineInstance();
		
		if (0 == pass)
		{
			lights_.clear();
			sm_light_indices_.clear();
			uint32_t const num_lights = scene_mgr.NumLights();
			bool with_ambient = false;
			for (uint32_t i = 0; i < num_lights; ++ i)
			{
				if (LT_Ambient == scene_mgr.GetLight(i)->Type())
				{
					with_ambient = true;
					break;
				}
			}
			if (!with_ambient)
			{
				LightSourcePtr ambient_light = MakeSharedPtr<AmbientLightSource>();
				ambient_light->Color(float3(0.1f, 0.1f, 0.1f));
				lights_.push_back(ambient_light);
				sm_light_indices_.push_back(-1);
			}

			uint32_t num_sm_2d_lights = 0;
			uint32_t num_sm_cube_lights = 0;
			for (uint32_t i = 0; i < num_lights; ++ i)
			{
				LightSourcePtr const & light = scene_mgr.GetLight(i);
				if (light->Enabled())
				{
					lights_.push_back(light);

					if (0 == (light->Attrib() & LSA_NoShadow))
					{
						switch (light->Type())
						{
						case LT_Spot:
							if (num_sm_2d_lights < NUM_SHADOWED_SPOT_LIGHTS)
							{
								sm_light_indices_.push_back(num_sm_2d_lights);
								++ num_sm_2d_lights;
							}
							break;

						case LT_Point:
							if (num_sm_cube_lights < NUM_SHADOWED_POINT_LIGHTS)
							{
								sm_light_indices_.push_back(num_sm_cube_lights);
								++ num_sm_cube_lights;
							}
							break;

						default:
							sm_light_indices_.push_back(-1);
							break;
						}
					}
					else
					{
						sm_light_indices_.push_back(-1);
					}
				}
			}

			bool has_opaque_objs = false;
			bool has_transparency_back_objs = false;
			bool has_transparency_front_objs = false;
			has_reflective_objs_ = false;
			has_simple_forward_objs_ = false;
			visible_scene_objs_.resize(0);
			uint32_t const num_scene_objs = scene_mgr.NumSceneObjects();
			for (uint32_t i = 0; i < num_scene_objs; ++ i)
			{
				SceneObjectPtr const & so = scene_mgr.GetSceneObject(i);
				if ((0 == (so->Attrib() & SceneObject::SOA_Overlay)) && so->Visible())
				{
					visible_scene_objs_.push_back(so.get());

					has_opaque_objs = true;

					if (so->TransparencyBackFace())
					{
						has_transparency_back_objs = true;
					}
					if (so->TransparencyFrontFace())
					{
						has_transparency_front_objs = true;
					}
					if (so->Reflection())
					{
						has_reflective_objs_ = true;
					}
					if (so->SimpleForward())
					{
						has_simple_forward_objs_ = true;
					}
				}
			}

			indirect_lighting_enabled_ = false;
			if (rsm_buffer_ && (illum_ != 1))
			{
				for (size_t i = 0; i != lights_.size(); ++ i)
				{
					if (lights_[i]->Attrib() & LSA_IndirectLighting)
					{
						indirect_lighting_enabled_ = true;
						break;
					}
				}
			}

			pass_scaned_.clear();
			for (uint32_t vpi = 0; vpi < viewports_.size(); ++ vpi)
			{
				PerViewport& pvp = viewports_[vpi];
				if (pvp.attrib & VPAM_Enabled)
				{
					pvp.g_buffer_enables[Opaque_GBuffer] = (pvp.attrib & VPAM_NoOpaque) ? false : has_opaque_objs;
					pvp.g_buffer_enables[TransparencyBack_GBuffer] = (pvp.attrib & VPAM_NoTransparencyBack) ? false : has_transparency_back_objs;
					pvp.g_buffer_enables[TransparencyFront_GBuffer] = (pvp.attrib & VPAM_NoTransparencyFront) ? false : has_transparency_front_objs;

					pvp.light_visibles.assign(lights_.size(), true);

					if (pvp.g_buffer_enables[Opaque_GBuffer])
					{
						if (!depth_texture_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueDepth, 0, 0));
						}

						if (mrt_g_buffer_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferMRT, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferMRT, 0, 1));
						}
						else
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferRT0, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferRT0, 0, 1));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferRT1, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueGBufferRT1, 0, 1));
						}
					}
					if (pvp.g_buffer_enables[TransparencyBack_GBuffer])
					{
						if (!depth_texture_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackDepth, 0, 0));
						}

						if (mrt_g_buffer_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackGBufferMRT, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackGBufferMRT, 0, 1));
						}
						else
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackGBufferRT0, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackGBufferRT0, 0, 1));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackGBufferRT1, 0, 0));
						}
					}
					if (pvp.g_buffer_enables[TransparencyFront_GBuffer])
					{
						if (!depth_texture_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontDepth, 0, 0));
						}

						if (mrt_g_buffer_support_)
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontGBufferMRT, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontGBufferMRT, 0, 1));
						}
						else
						{
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontGBufferRT0, 0, 0));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontGBufferRT0, 0, 1));
							pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontGBufferRT1, 0, 0));
						}
					}
				}
			}

			for (uint32_t i = 0; i < lights_.size(); ++ i)
			{
				uint32_t vpi = 0;
				LightSourcePtr const & light = lights_[i];
				if (light->Enabled())
				{
					int type = light->Type();
					int32_t attr = light->Attrib();
					switch (type)
					{
					case LT_Spot:
						{
							int sm_seq;
							if (attr & LSA_IndirectLighting)
							{
								if (rsm_buffer_ && (illum_ != 1))
								{
									sm_seq = 2;
								}
								else
								{
									sm_seq = 1;
								}
							}
							else
							{
								if (0 == (attr & LSA_NoShadow))
								{
									sm_seq = 1;
								}
								else
								{
									sm_seq = 0;
								}
							}

							switch (sm_seq)
							{
							case 1:
								if (depth_texture_support_)
								{
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMap, i, 0));
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMap, i, 1));
								}
								else
								{
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMapWODepthTexture, i, 0));
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMapWODepthTexture, i, 1));
								}
								break;

							case 2:
								pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenReflectiveShadowMap, i, 0));
								pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenReflectiveShadowMap, i, 1));
								break;

							default:
								break;
							}
						}
						break;

					case LT_Point:
						if (0 == (attr & LSA_NoShadow))
						{
							for (int j = 0; j < 7; ++ j)
							{
								if (depth_texture_support_)
								{
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMap, i, j));
								}
								else
								{
									pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMapWODepthTexture, i, j));
								}
							}
						}
						break;

					default:
						if (0 == (attr & LSA_NoShadow))
						{							
							if (depth_texture_support_)
							{
								pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMap, i, 0));
							}
							else
							{
								pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_GenShadowMapWODepthTexture, i, 0));
							}
						}
						break;
					}
				}
			}

			for (uint32_t vpi = 0; vpi < viewports_.size(); ++ vpi)
			{
				PerViewport& pvp = viewports_[vpi];
				if (pvp.attrib & VPAM_Enabled)
				{
					for (uint32_t i = 0; i < lights_.size(); ++ i)
					{
						LightSourcePtr const & light = lights_[i];
						if (light->Enabled())
						{
							int type = light->Type();
							int32_t attr = light->Attrib();
							switch (type)
							{
							case LT_Spot:
								{
									float4x4 const & inv_light_view = light->SMCamera(0)->InverseViewMatrix();

									float const scale = light->CosOuterInner().w();
									float4x4 mat = MathLib::scaling(scale * light_scale_, scale * light_scale_, light_scale_);
									float4x4 light_model = mat * inv_light_view;

									if (scene_mgr.OBBVisible(MathLib::transform_obb(cone_obb_, light_model)))
									{
										if (attr & LSA_IndirectLighting)
										{
											if (rsm_buffer_ && (illum_ != 1))
											{
												pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_IndirectLighting, i, 0));
											}
										}
										pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_Lighting, i, 0));
									}
									else
									{
										pvp.light_visibles[i] = false;
									}
								}
								break;

							case LT_Point:
								{
									float3 const & p = light->Position();
									
									float4x4 light_model = MathLib::scaling(light_scale_, light_scale_, light_scale_)
										* MathLib::translation(p);

									if (scene_mgr.OBBVisible(MathLib::transform_obb(box_obb_, light_model)))
									{
										pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_Lighting, i, 0));
									}
									else
									{
										pvp.light_visibles[i] = false;
									}
								}
								break;

							default:
								pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_Lighting, i, 0));
								break;
							}
						}
					}
					pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueShading, 0, 0));
					if (pvp.g_buffer_enables[TransparencyBack_GBuffer])
					{
						pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackShading, 0, 0));
					}
					if (pvp.g_buffer_enables[TransparencyFront_GBuffer])
					{
						pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontShading, 0, 0));
					}
					pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueSpecialShading, 0, 0));
					if (pvp.g_buffer_enables[TransparencyBack_GBuffer])
					{
						pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyBackSpecialShading, 0, 0));
					}
					if (pvp.g_buffer_enables[TransparencyFront_GBuffer])
					{
						pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_TransparencyFrontSpecialShading, 0, 0));
					}
					pass_scaned_.push_back(this->ComposePassScanCode(vpi, PT_OpaqueSpecialShading, 0, 1));
				}
			}
		}

		uint32_t vp_index;
		int32_t pass_type, org_no, index_in_pass;
		this->DecomposePassScanCode(vp_index, pass_type, org_no, index_in_pass, pass_scaned_[pass]);

		active_viewport_ = vp_index;

		PerViewport& pvp = viewports_[vp_index];

		if ((pass_type != PT_Lighting) && (pass_type != PT_IndirectLighting)
			&& (pass_type != PT_OpaqueShading) && (pass_type != PT_TransparencyBackShading) && (pass_type != PT_TransparencyFrontShading))
		{
			typedef KLAYGE_DECLTYPE(visible_scene_objs_) VisibleSceneObjsType;
			KLAYGE_FOREACH(VisibleSceneObjsType::reference deo, visible_scene_objs_)
			{
				deo->Pass(static_cast<PassType>(pass_type));
			}
		}

		if ((PT_OpaqueDepth == pass_type) || (PT_TransparencyBackDepth == pass_type) || (PT_TransparencyFrontDepth == pass_type))
		{
			// Pre depth for no depth texture support platforms

			CameraPtr const & camera = pvp.frame_buffer->GetViewport()->camera;
			for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
			{
				if (!depth_texture_support_)
				{
					pvp.pre_depth_buffers[i]->GetViewport()->camera = camera;
				}
				pvp.g_buffers[i]->GetViewport()->camera = camera;
				if (!mrt_g_buffer_support_)
				{
					pvp.g_buffers_rt1[i]->GetViewport()->camera = camera;
				}
				pvp.lighting_buffers[i]->GetViewport()->camera = camera;
				pvp.shading_buffers[i]->GetViewport()->camera = camera;
				pvp.curr_merged_shading_buffers[i]->GetViewport()->camera = camera;
				pvp.curr_merged_depth_buffers[i]->GetViewport()->camera = camera;
				pvp.prev_merged_shading_buffers[i]->GetViewport()->camera = camera;
				pvp.prev_merged_depth_buffers[i]->GetViewport()->camera = camera;
			}
			pvp.shadowing_buffer->GetViewport()->camera = camera;

			pvp.inv_view = camera->InverseViewMatrix();
			pvp.inv_proj = camera->InverseProjMatrix();
			pvp.proj_to_prev = pvp.inv_proj * pvp.inv_view * pvp.view * pvp.proj;
			pvp.view = camera->ViewMatrix();
			pvp.proj = camera->ProjMatrix();
			pvp.depth_near_far_invfar = float3(camera->NearPlane(), camera->FarPlane(), 1 / camera->FarPlane());

			if (depth_texture_support_)
			{
				float q = camera->FarPlane() / (camera->FarPlane() - camera->NearPlane());
				float2 near_q(camera->NearPlane() * q, q);
				depth_to_linear_pp_->SetParam(0, near_q);
			}

			if (PT_OpaqueDepth == pass_type)
			{
				re.BindFrameBuffer(pvp.pre_depth_buffers[Opaque_GBuffer]);
				re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 1.0f, 0);
				return App3DFramework::URV_Need_Flush | App3DFramework::URV_Opaque_Only;
			}
			else
			{
				if (PT_TransparencyBackDepth == pass_type)
				{
					re.BindFrameBuffer(pvp.pre_depth_buffers[TransparencyBack_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 0.0f, 128);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Back_Only;
				}
				else
				{
					BOOST_ASSERT(PT_TransparencyFrontDepth == pass_type);

					re.BindFrameBuffer(pvp.pre_depth_buffers[TransparencyFront_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 1.0f, 128);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Front_Only;
				}
			}
		}
		else if ((PT_OpaqueGBufferRT0 == pass_type) || (PT_TransparencyBackGBufferRT0 == pass_type) || (PT_TransparencyFrontGBufferRT0 == pass_type)
			|| (PT_OpaqueGBufferMRT == pass_type) || (PT_TransparencyBackGBufferMRT == pass_type) || (PT_TransparencyFrontGBufferMRT == pass_type))
		{
			if (0 == index_in_pass)
			{
				// Generate G-Buffer

				CameraPtr const & camera = pvp.frame_buffer->GetViewport()->camera;
				for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
				{
					if (!depth_texture_support_)
					{
						pvp.pre_depth_buffers[i]->GetViewport()->camera = camera;
					}
					pvp.g_buffers[i]->GetViewport()->camera = camera;
					if (!mrt_g_buffer_support_)
					{
						pvp.g_buffers_rt1[i]->GetViewport()->camera = camera;
					}
					pvp.lighting_buffers[i]->GetViewport()->camera = camera;
					pvp.shading_buffers[i]->GetViewport()->camera = camera;
					pvp.curr_merged_shading_buffers[i]->GetViewport()->camera = camera;
					pvp.curr_merged_depth_buffers[i]->GetViewport()->camera = camera;
					pvp.prev_merged_shading_buffers[i]->GetViewport()->camera = camera;
					pvp.prev_merged_depth_buffers[i]->GetViewport()->camera = camera;
				}
				pvp.shadowing_buffer->GetViewport()->camera = camera;

				pvp.view = camera->ViewMatrix();
				pvp.proj = camera->ProjMatrix();
				pvp.inv_view = camera->InverseViewMatrix();
				pvp.inv_proj = camera->InverseProjMatrix();
				pvp.depth_near_far_invfar = float3(camera->NearPlane(), camera->FarPlane(), 1 / camera->FarPlane());

				if (depth_texture_support_)
				{
					float q = camera->FarPlane() / (camera->FarPlane() - camera->NearPlane());
					float2 near_q(camera->NearPlane() * q, q);
					depth_to_linear_pp_->SetParam(0, near_q);
				}

				*depth_near_far_invfar_param_ = pvp.depth_near_far_invfar;

				if ((PT_OpaqueGBufferRT0 == pass_type) || (PT_OpaqueGBufferMRT == pass_type))
				{
					re.BindFrameBuffer(pvp.g_buffers[Opaque_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 1.0f, 0);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Opaque_Only;
				}
				else
				{
					if ((PT_TransparencyBackGBufferRT0 == pass_type) || (PT_TransparencyBackGBufferMRT == pass_type))
					{
						re.BindFrameBuffer(pvp.g_buffers[TransparencyBack_GBuffer]);
						re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 0.0f, 128);
						return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Back_Only;
					}
					else
					{
						BOOST_ASSERT((PT_TransparencyFrontGBufferRT0 == pass_type) || (PT_TransparencyFrontGBufferMRT == pass_type));

						re.BindFrameBuffer(pvp.g_buffers[TransparencyFront_GBuffer]);
						re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 1.0f, 128);
						return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Front_Only;
					}
				}
			}
			else
			{
				if ((PT_OpaqueGBufferRT0 == pass_type) || (PT_OpaqueGBufferMRT == pass_type))
				{
					pvp.g_buffer_rt0_texs[Opaque_GBuffer]->BuildMipSubLevels();

					if (depth_texture_support_)
					{
						depth_to_linear_pp_->InputPin(0, pvp.g_buffer_ds_texs[Opaque_GBuffer]);
						depth_to_linear_pp_->OutputPin(0, pvp.g_buffer_depth_texs[Opaque_GBuffer]);
						depth_to_linear_pp_->Apply();
					}

					pvp.g_buffer_depth_texs[Opaque_GBuffer]->BuildMipSubLevels();

					if (indirect_lighting_enabled_ && !(pvp.attrib & VPAM_NoGI))
					{
						pvp.il_layer->UpdateGBuffer(pvp.frame_buffer->GetViewport()->camera);
					}

					if (!decals_.empty())
					{
						PassType pt = mrt_g_buffer_support_ ? PT_OpaqueGBufferMRT : PT_OpaqueGBufferRT0;
						re.BindFrameBuffer(pvp.g_buffers[Opaque_GBuffer]);
						typedef KLAYGE_DECLTYPE(decals_) DecalsType;
						KLAYGE_FOREACH(DecalsType::reference de, decals_)
						{
							de->Pass(pt);
							de->Render();
						}
					}
				}
				else
				{
					if ((PT_TransparencyBackGBufferRT0 == pass_type) || (PT_TransparencyBackGBufferMRT == pass_type))
					{
						pvp.g_buffer_rt0_texs[TransparencyBack_GBuffer]->BuildMipSubLevels();

						if (depth_texture_support_)
						{
							depth_to_linear_pp_->InputPin(0, pvp.g_buffer_ds_texs[TransparencyBack_GBuffer]);
							depth_to_linear_pp_->OutputPin(0, pvp.g_buffer_depth_texs[TransparencyBack_GBuffer]);
							depth_to_linear_pp_->Apply();
						}

						pvp.g_buffer_depth_texs[TransparencyBack_GBuffer]->BuildMipSubLevels();
					}
					else
					{
						pvp.g_buffer_rt0_texs[TransparencyFront_GBuffer]->BuildMipSubLevels();

						if (depth_texture_support_)
						{
							depth_to_linear_pp_->InputPin(0, pvp.g_buffer_ds_texs[TransparencyFront_GBuffer]);
							depth_to_linear_pp_->OutputPin(0, pvp.g_buffer_depth_texs[TransparencyFront_GBuffer]);
							depth_to_linear_pp_->Apply();
						}

						pvp.g_buffer_depth_texs[TransparencyFront_GBuffer]->BuildMipSubLevels();
					}
				}

				return App3DFramework::URV_Flushed;
			}
		}
		else if ((PT_OpaqueGBufferRT1 == pass_type) || (PT_TransparencyBackGBufferRT1 == pass_type) || (PT_TransparencyFrontGBufferRT1 == pass_type))
		{
			if (0 == index_in_pass)
			{
				if (PT_OpaqueGBufferRT1 == pass_type)
				{
					re.BindFrameBuffer(pvp.g_buffers_rt1[Opaque_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color, Color(0, 0, 0, 0), 1.0f, 0);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Opaque_Only;
				}
				else if (PT_TransparencyBackGBufferRT1 == pass_type)
				{
					re.BindFrameBuffer(pvp.g_buffers_rt1[TransparencyBack_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color, Color(0, 0, 0, 0), 1.0f, 0);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Back_Only;
				}
				else
				{
					re.BindFrameBuffer(pvp.g_buffers_rt1[TransparencyFront_GBuffer]);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color, Color(0, 0, 0, 0), 1.0f, 0);
					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Front_Only;
				}
			}
			else
			{
				if (!decals_.empty())
				{
					re.BindFrameBuffer(pvp.g_buffers_rt1[Opaque_GBuffer]);
					typedef KLAYGE_DECLTYPE(decals_) DecalsType;
					KLAYGE_FOREACH(DecalsType::reference de, decals_)
					{
						de->Pass(PT_OpaqueGBufferRT1);
						de->Render();
					}
				}

				return App3DFramework::URV_Flushed;
			}
		}
		else
		{
			if ((PT_OpaqueShading == pass_type) || (PT_TransparencyBackShading == pass_type) || (PT_TransparencyFrontShading == pass_type)
				|| (PT_OpaqueSpecialShading == pass_type) || (PT_TransparencyBackSpecialShading == pass_type) || (PT_TransparencyFrontSpecialShading == pass_type))
			{
				if (0 == index_in_pass)
				{
					switch (pass_type)
					{
					case PT_OpaqueShading:
						if ((indirect_lighting_enabled_ && !(pvp.attrib & VPAM_NoGI)) && (illum_ != 1))
						{
							pvp.il_layer->CalcIndirectLighting(pvp.prev_merged_shading_tex, pvp.proj_to_prev);
							this->AccumulateToLightingTex(vp_index);
						}

						if (pvp.ssvo_enabled && !(pvp.attrib & VPAM_NoSSVO))
						{
							ssvo_pp_->InputPin(0, pvp.g_buffer_rt0_texs[Opaque_GBuffer]);
							ssvo_pp_->InputPin(1, pvp.g_buffer_depth_texs[Opaque_GBuffer]);
							ssvo_pp_->OutputPin(0, pvp.small_ssvo_tex);
							ssvo_pp_->Apply();

							ssvo_blur_pp_->InputPin(0, pvp.small_ssvo_tex);
							ssvo_blur_pp_->InputPin(1, pvp.g_buffer_depth_texs[Opaque_GBuffer]);
							ssvo_blur_pp_->OutputPin(0, pvp.lighting_texs[Opaque_GBuffer]);
							ssvo_blur_pp_->Apply();
						}

						re.BindFrameBuffer(pvp.shading_buffers[Opaque_GBuffer]);
						*g_buffer_tex_param_ = pvp.g_buffer_rt0_texs[Opaque_GBuffer];
						*g_buffer_1_tex_param_ = pvp.g_buffer_rt1_texs[Opaque_GBuffer];
						*depth_tex_param_ = pvp.g_buffer_depth_texs[Opaque_GBuffer];
						*lighting_tex_param_ = pvp.lighting_texs[Opaque_GBuffer];
						*light_volume_mv_param_ = pvp.inv_proj;

						re.Render(*technique_no_lighting_, *rl_quad_);
						re.Render(*technique_shading_, *rl_quad_);

						return App3DFramework::URV_Flushed;

					case PT_TransparencyBackShading:
						re.BindFrameBuffer(pvp.shading_buffers[TransparencyBack_GBuffer]);
						*g_buffer_tex_param_ = pvp.g_buffer_rt0_texs[TransparencyBack_GBuffer];
						*g_buffer_1_tex_param_ = pvp.g_buffer_rt1_texs[TransparencyBack_GBuffer];
						*depth_tex_param_ = pvp.g_buffer_depth_texs[TransparencyBack_GBuffer];
						*lighting_tex_param_ = pvp.lighting_texs[TransparencyBack_GBuffer];
						*light_volume_mv_param_ = pvp.inv_proj;

						re.CurFrameBuffer()->Attached(FrameBuffer::ATT_Color0)->ClearColor(Color(0, 0, 0, 0));
						re.Render(*technique_shading_, *rl_quad_);

						return App3DFramework::URV_Flushed;

					case PT_TransparencyFrontShading:
						re.BindFrameBuffer(pvp.shading_buffers[TransparencyFront_GBuffer]);
						*g_buffer_tex_param_ = pvp.g_buffer_rt0_texs[TransparencyFront_GBuffer];
						*g_buffer_1_tex_param_ = pvp.g_buffer_rt1_texs[TransparencyFront_GBuffer];
						*depth_tex_param_ = pvp.g_buffer_depth_texs[TransparencyFront_GBuffer];								
						*lighting_tex_param_ = pvp.lighting_texs[TransparencyFront_GBuffer];
						*light_volume_mv_param_ = pvp.inv_proj;

						re.CurFrameBuffer()->Attached(FrameBuffer::ATT_Color0)->ClearColor(Color(0, 0, 0, 0));
						re.Render(*technique_shading_, *rl_quad_);

						return App3DFramework::URV_Flushed;

					case PT_OpaqueSpecialShading:
						re.BindFrameBuffer(pvp.shading_buffers[Opaque_GBuffer]);
						return App3DFramework::URV_Need_Flush | App3DFramework::URV_Opaque_Only | App3DFramework::URV_Special_Shading_Only;

					case PT_TransparencyBackSpecialShading:
						re.BindFrameBuffer(pvp.shading_buffers[TransparencyBack_GBuffer]);
						return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Back_Only | App3DFramework::URV_Special_Shading_Only;

					case PT_TransparencyFrontSpecialShading:
					default:
						re.BindFrameBuffer(pvp.shading_buffers[TransparencyFront_GBuffer]);
						return App3DFramework::URV_Need_Flush | App3DFramework::URV_Transparency_Front_Only | App3DFramework::URV_Special_Shading_Only;
					}
				}
				else
				{
					if (has_reflective_objs_ && ssr_enabled_)
					{
						for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
						{
							if (pvp.g_buffer_enables[i])
							{
								re.BindFrameBuffer(pvp.shading_buffers[i]);
								ssr_pp_->InputPin(0, pvp.g_buffer_rt0_texs[Opaque_GBuffer]);
								ssr_pp_->InputPin(1, pvp.g_buffer_rt1_texs[Opaque_GBuffer]);
								ssr_pp_->InputPin(2, pvp.g_buffer_depth_texs[Opaque_GBuffer]);
								ssr_pp_->InputPin(3, pvp.prev_merged_shading_tex);
								ssr_pp_->InputPin(4, pvp.g_buffer_depth_texs[i]);
								ssr_pp_->Apply();
							}
						}
					}

					if (atmospheric_pp_)
					{
						for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
						{
							if (pvp.g_buffer_enables[i])
							{
								re.BindFrameBuffer(pvp.shading_buffers[i]);
								atmospheric_pp_->SetParam(0, pvp.inv_proj);
								atmospheric_pp_->InputPin(0, pvp.g_buffer_depth_texs[i]);
								atmospheric_pp_->Render();
							}
						}
					}

					for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
					{
						if (pvp.g_buffer_enables[i])
						{
							re.BindFrameBuffer(pvp.curr_merged_shading_buffers[i]);
							*shading_tex_param_ = pvp.shading_texs[i];
							re.Render(*technique_merge_shadings_[i != 0], *rl_quad_);
						}
					}

					for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
					{
						if (pvp.g_buffer_enables[i])
						{
							re.BindFrameBuffer(pvp.curr_merged_depth_buffers[i]);
							*depth_tex_param_ = pvp.g_buffer_depth_texs[i];
							re.Render(*technique_merge_depths_[i != 0], *rl_quad_);
						}
					}

					re.BindFrameBuffer(pvp.frame_buffer);
					{
						*depth_tex_param_ = pvp.curr_merged_depth_tex;

						CameraPtr const & camera = pvp.frame_buffer->GetViewport()->camera;
						float q = camera->FarPlane() / (camera->FarPlane() - camera->NearPlane());
						float2 near_q(camera->NearPlane() * q, q);
						*near_q_param_ = near_q;
					}
					App3DFramework& app = Context::Instance().AppInstance();
					if ((app.FrameTime() < 1.0f / 30) && taa_enabled_)
					{
						taa_pp_->InputPin(0, pvp.curr_merged_shading_tex);
						taa_pp_->InputPin(1, pvp.prev_merged_shading_tex);
						taa_pp_->Render();
						re.Render(*technique_copy_depth_, *rl_quad_);
					}
					else
					{
						*shading_tex_param_ = pvp.curr_merged_shading_tex;
						re.Render(*technique_copy_shading_depth_, *rl_quad_);
					}

					std::swap(pvp.curr_merged_shading_buffers, pvp.prev_merged_shading_buffers);
					std::swap(pvp.curr_merged_shading_tex, pvp.prev_merged_shading_tex);
					std::swap(pvp.curr_merged_depth_buffers, pvp.prev_merged_depth_buffers);
					std::swap(pvp.curr_merged_depth_tex, pvp.prev_merged_depth_tex);

					uint32_t urv;
					if (has_simple_forward_objs_ && !(pvp.attrib & VPAM_NoSimpleForward))
					{
						typedef KLAYGE_DECLTYPE(visible_scene_objs_) VisibleSceneObjsType;
						KLAYGE_FOREACH(VisibleSceneObjsType::reference deo, visible_scene_objs_)
						{
							deo->Pass(PT_SimpleForward);
						}

						urv = App3DFramework::URV_Need_Flush | App3DFramework::URV_Simple_Forward_Only;
						if (pass_scaned_.size() - 1 == pass)
						{
							lights_.clear();
							sm_light_indices_.clear();
							urv |= App3DFramework::URV_Finished;
						}
					}
					else
					{
						urv = App3DFramework::URV_Flushed;
						if (pass_scaned_.size() - 1 == pass)
						{
							lights_.clear();
							sm_light_indices_.clear();
							urv = App3DFramework::URV_Finished;
						}
					}

					return urv;
				}
			}
			else
			{
				LightSourcePtr const & light = lights_[org_no];

				LightType type = light->Type();
				int32_t attr = light->Attrib();

				RenderLayoutPtr rl;
				switch (type)
				{
				case LT_Point:
				case LT_Spot:
					{
						CameraPtr sm_camera;
						float3 dir_es(0, 0, 0);
						if (LT_Spot == type)
						{
							dir_es = MathLib::transform_normal(light->Direction(), pvp.view);
							sm_camera = light->SMCamera(0);
						}
						else
						{
							if (index_in_pass < 6)
							{
								std::pair<float3, float3> ad = CubeMapViewVector<float>(static_cast<Texture::CubeFaces>(index_in_pass));
								dir_es = MathLib::transform_normal(MathLib::transform_quat(ad.first, light->Rotation()), pvp.view);
								sm_camera = light->SMCamera(index_in_pass);
							}
							else
							{
								std::pair<float3, float3> ad = CubeMapViewVector<float>(static_cast<Texture::CubeFaces>(5));
								dir_es = MathLib::transform_normal(MathLib::transform_quat(ad.first, light->Rotation()), pvp.view);
								sm_camera = light->SMCamera(5);
							}
						}
						float4 light_dir_es_actived = float4(dir_es.x(), dir_es.y(), dir_es.z(), 0);

						float4x4 light_to_view, light_to_proj;
						if (sm_camera)
						{
							sm_buffer_->GetViewport()->camera = sm_camera;

							*light_view_proj_param_ = pvp.inv_view * sm_camera->ViewMatrix() * sm_camera->ProjMatrix();

							light_to_view = sm_camera->InverseViewMatrix() * pvp.view;
							light_to_proj = light_to_view * pvp.proj;
						}

						if ((PT_GenShadowMap == pass_type) || (PT_GenReflectiveShadowMap == pass_type))
						{
							if (depth_texture_support_)
							{
								float q = sm_camera->FarPlane() / (sm_camera->FarPlane() - sm_camera->NearPlane());
								float2 near_q(sm_camera->NearPlane() * q, q);
								depth_to_vsm_pp_->SetParam(0, near_q);
							
								float4x4 inv_sm_proj = sm_camera->InverseProjMatrix();
								depth_to_vsm_pp_->SetParam(1, inv_sm_proj);
							}
						}

						float3 const & p = light->Position();
						float3 loc_es = MathLib::transform_coord(p, pvp.view);
						float4 light_pos_es_actived = float4(loc_es.x(), loc_es.y(), loc_es.z(), 1);

						switch (type)
						{
						case LT_Spot:
							{
								light_pos_es_actived.w() = light->CosOuterInner().x();
								light_dir_es_actived.w() = light->CosOuterInner().y();

								rl = rl_cone_;
								float const scale = light->CosOuterInner().w();
								float4x4 light_model = MathLib::scaling(scale * light_scale_, scale * light_scale_, light_scale_);
								*light_volume_mv_param_ = light_model * light_to_view;
								*light_volume_mvp_param_ = light_model * light_to_proj;
							}
							break;

						case LT_Point:
							if (PT_Lighting == pass_type)
							{
								rl = rl_box_;
								float4x4 light_model = MathLib::scaling(light_scale_, light_scale_, light_scale_)
									* MathLib::to_matrix(light->Rotation()) * MathLib::translation(p);
								*light_volume_mv_param_ = light_model * pvp.view;
								*light_volume_mvp_param_ = light_model * pvp.view * pvp.proj;
								*view_to_light_model_param_ = pvp.inv_view * MathLib::inverse(light_model);
							}
							else
							{
								rl = rl_pyramid_;
								*light_volume_mv_param_ = light_to_view;
								*light_volume_mvp_param_ = light_to_proj;
							}
							break;

						default:
							BOOST_ASSERT(false);
							break;
						}
					
						*light_pos_es_param_ = light_pos_es_actived;
						*light_dir_es_param_ = light_dir_es_actived;
					}
					break;

				case LT_Directional:
					{
						float3 dir_es = MathLib::transform_normal(light->Direction(), pvp.view);
						*light_dir_es_param_ = float4(dir_es.x(), dir_es.y(), dir_es.z(), 0);
					}
					rl = rl_quad_;
					*light_volume_mv_param_ = pvp.inv_proj;
					*light_volume_mvp_param_ = float4x4::Identity();
					break;

				case LT_Ambient:
					{
						float3 dir_es = MathLib::transform_normal(float3(0, 1, 0), pvp.view);
						*light_dir_es_param_ = float4(dir_es.x(), dir_es.y(), dir_es.z(), 0);
					}
					rl = rl_quad_;
					*light_volume_mv_param_ = pvp.inv_proj;
					*light_volume_mvp_param_ = float4x4::Identity();
					break;

				default:
					BOOST_ASSERT(false);
					break;
				}

				if ((index_in_pass > 0)
					&& ((PT_GenShadowMap == pass_type) || (PT_GenShadowMapWODepthTexture == pass_type)
						|| (PT_GenReflectiveShadowMap == pass_type)))
				{
					if (0 == (attr & LSA_NoShadow))
					{
						if (depth_texture_support_)
						{
							depth_to_vsm_pp_->Apply();
						}

						if (LT_Point == type)
						{
							sm_filter_pp_->InputPin(0, sm_tex_);
							sm_filter_pp_->OutputPin(0, blur_sm_cube_texs_[sm_light_indices_[org_no]], 0, 0, index_in_pass - 1);
							sm_filter_pp_->Apply();
							if (6 == index_in_pass)
							{
								return App3DFramework::URV_Flushed;
							}
						}
						else
						{
							sm_filter_pp_->InputPin(0, sm_tex_);
							sm_filter_pp_->OutputPin(0, blur_sm_2d_texs_[sm_light_indices_[org_no]]);
							sm_filter_pp_->Apply();
							if (1 == index_in_pass)
							{
								return App3DFramework::URV_Flushed;
							}
						}
					}
				}

				if (PT_GenReflectiveShadowMap == pass_type)
				{
					rsm_buffer_->GetViewport()->camera = sm_buffer_->GetViewport()->camera;
					re.BindFrameBuffer(rsm_buffer_);
					re.CurFrameBuffer()->Clear(FrameBuffer::CBM_Color | FrameBuffer::CBM_Depth | FrameBuffer::CBM_Stencil, Color(0, 0, 0, 0), 1.0f, 0);

					return App3DFramework::URV_Need_Flush | App3DFramework::URV_Opaque_Only;
				}
				else if (PT_IndirectLighting == pass_type)
				{
					if (depth_texture_support_)
					{
						depth_to_vsm_pp_->Apply();
					}

					pvp.il_layer->UpdateRSM(rsm_buffer_->GetViewport()->camera, light);

					return App3DFramework::URV_Flushed;
				}

				if ((PT_GenShadowMap == pass_type) || (PT_GenShadowMapWODepthTexture == pass_type))
				{
					// Shadow map generation
					re.BindFrameBuffer(sm_buffer_);
					re.CurFrameBuffer()->Attached(FrameBuffer::ATT_DepthStencil)->ClearDepth(1.0f);

					return App3DFramework::URV_Need_Flush;
				}
				else //if (PT_Lighting == pass_type)
				{
					if (LT_Point == type)
					{
						*projective_map_cube_tex_param_ = light->ProjectiveTexture();
					}
					else
					{
						*projective_map_2d_tex_param_ = light->ProjectiveTexture();
					}

					*light_attrib_param_ = float4(attr & LSA_NoDiffuse ? 0.0f : 1.0f, attr & LSA_NoSpecular ? 0.0f : 1.0f,
						attr & LSA_NoShadow ? -1.0f : 1.0f, light->ProjectiveTexture() ? 1.0f : -1.0f);
					*light_color_param_ = light->Color();
					*light_falloff_param_ = light->Falloff();

					for (size_t i = 0; i < pvp.g_buffers.size(); ++ i)
					{
						if (pvp.g_buffer_enables[i])
						{
							// Shadowing

							int32_t light_index = sm_light_indices_[org_no];
							if ((light_index >= 0) && (0 == (light->Attrib() & LSA_NoShadow)))
							{
								switch (light->Type())
								{
								case LT_Spot:
									*shadow_map_2d_tex_param_ = blur_sm_2d_texs_[light_index];
									break;

								case LT_Point:
									*shadow_map_cube_tex_param_ = blur_sm_cube_texs_[light_index];
									break;

								default:
									break;
								}
							}

							re.BindFrameBuffer(pvp.shadowing_buffer);
							re.CurFrameBuffer()->Attached(FrameBuffer::ATT_Color0)->ClearColor(Color(1, 1, 1, 1));

							*g_buffer_tex_param_ = pvp.g_buffer_rt0_texs[i];
							*depth_tex_param_ = pvp.g_buffer_depth_texs[i];
							*inv_width_height_param_ = float2(1.0f / pvp.frame_buffer->GetViewport()->width, 1.0f / pvp.frame_buffer->GetViewport()->height);
							*shadowing_tex_param_ = pvp.shadowing_tex;

							if (0 == (attr & LSA_NoShadow))
							{
								re.Render(*technique_shadows_[type], *rl);
							}


							// Lighting

							re.BindFrameBuffer(pvp.lighting_buffers[i]);
							// Clear stencil to 0 with write mask
							re.Render(*technique_clear_stencil_, *rl_quad_);

							if ((LT_Point == type) || (LT_Spot == type))
							{
								re.Render(*technique_light_stencil_, *rl);
							}

							re.Render(*technique_lights_[type], *rl);
						}
					}

					return App3DFramework::URV_Flushed;
				}
			}
		}
	}

	void DeferredRenderingLayer::SetupViewportGI(uint32_t vp)
	{
		PerViewport& pvp = viewports_[vp];
		if (pvp.ssgi_enable)
		{
			pvp.il_layer = MakeSharedPtr<SSGILayer>();
		}
		else
		{
			if (rsm_buffer_)
			{
				pvp.il_layer = MakeSharedPtr<MultiResSILLayer>();
			}
			else
			{
				pvp.il_layer.reset();
			}
		}

		if (pvp.il_layer && pvp.g_buffer_rt0_texs[Opaque_GBuffer] && rsm_texs_[0])
		{
			pvp.il_layer->GBuffer(pvp.g_buffer_rt0_texs[Opaque_GBuffer], pvp.g_buffer_rt0_texs[Opaque_GBuffer],
				pvp.g_buffer_depth_texs[Opaque_GBuffer]);
			pvp.il_layer->RSM(rsm_texs_[0], rsm_texs_[1], sm_tex_);
		}
	}

	void DeferredRenderingLayer::AccumulateToLightingTex(uint32_t vp)
	{
		PerViewport& pvp = viewports_[vp];
		PostProcessPtr const & copy_to_light_buffer_pp = (0 == illum_) ? copy_to_light_buffer_pp_ : copy_to_light_buffer_i_pp_;
		copy_to_light_buffer_pp->SetParam(0, indirect_scale_ * 256 / VPL_COUNT);
		copy_to_light_buffer_pp->SetParam(1, float2(1.0f / pvp.g_buffer_rt0_texs[Opaque_GBuffer]->Width(0), 1.0f / pvp.g_buffer_rt0_texs[Opaque_GBuffer]->Height(0)));
		copy_to_light_buffer_pp->SetParam(2, pvp.depth_near_far_invfar);
		copy_to_light_buffer_pp->SetParam(3, pvp.inv_proj);
		copy_to_light_buffer_pp->InputPin(0, pvp.il_layer->IndirectLightingTex());
		copy_to_light_buffer_pp->InputPin(1, pvp.g_buffer_rt0_texs[Opaque_GBuffer]);
		copy_to_light_buffer_pp->InputPin(2, pvp.g_buffer_depth_texs[Opaque_GBuffer]);
		copy_to_light_buffer_pp->OutputPin(0, pvp.lighting_texs[Opaque_GBuffer]);
		copy_to_light_buffer_pp->Apply();
	}

	void DeferredRenderingLayer::DisplayIllum(int illum)
	{
		illum_ = illum;
	}

	void DeferredRenderingLayer::IndirectScale(float scale)
	{
		indirect_scale_ = scale;
	}

	uint32_t DeferredRenderingLayer::ComposePassScanCode(uint32_t vp_index, int32_t pass_type, int32_t org_no, int32_t index_in_pass)
	{
		return (vp_index << 28) | (pass_type << 20) | (org_no << 8) | index_in_pass;
	}

	void DeferredRenderingLayer::DecomposePassScanCode(uint32_t& vp_index, int32_t& pass_type, int32_t& org_no, int32_t& index_in_pass, uint32_t code)
	{
		vp_index = code >> 28;				//  4 bits, [31 - 28]
		pass_type = (code >> 20) & 0xFF;	//  8 bits, [27 - 20]
		org_no = (code >> 8) & 0xFFF;		// 12 bits, [19 - 8]
		index_in_pass = (code) & 0xFF;		//  8 bits, [7 -  0]
	}
}
