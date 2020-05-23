#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include <algorithm>

BspRenderer::BspRenderer(Bsp* map, ShaderProgram* pipeline) {
	this->map = map;
	this->pipeline = pipeline;

	loadTextures();
	loadLightmaps();
	preRenderFaces();
	preRenderEnts();

	uint sTexId = glGetUniformLocation(pipeline->ID, "sTex");
	uint opacityId = glGetUniformLocation(pipeline->ID, "opacity");

	glUniform1i(sTexId, 0);

	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		uint sLightmapTexIds = glGetUniformLocation(pipeline->ID, ("sLightmapTex" + to_string(s)).c_str());

		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds, s + 1);
	}
}

void BspRenderer::loadTextures() {
	whiteTex = new Texture(16, 16);
	memset(whiteTex->data, 255, 16 * 16 * sizeof(COLOR3));
	whiteTex->upload();

	vector<Wad*> wads;
	vector<string> wadNames;
	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->keyvalues["classname"] == "worldspawn") {
			wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	vector<string> tryPaths = {
		g_game_path + "/svencoop/",
		g_game_path + "/svencoop_addon/",
		g_game_path + "/svencoop_downloads/",
		g_game_path + "/svencoop_hd/"
	};

	
	for (int i = 0; i < wadNames.size(); i++) {
		string path;
		for (int k = 0; k < tryPaths.size(); k++) {
			string tryPath = tryPaths[k] + wadNames[i];
			if (fileExists(tryPath)) {
				path = tryPath;
				break;
			}
		}

		if (path.empty()) {
			printf("Missing WAD: %s\n", wadNames[i].c_str());
			continue;
		}

		printf("Loading WAD %s\n", path.c_str());
		Wad* wad = new Wad(path);
		wad->readInfo();
		wads.push_back(wad);
	}

	glTextures = new Texture * [map->textureCount];
	for (int i = 0; i < map->textureCount; i++) {
		int32_t texOffset = ((int32_t*)map->textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

		COLOR3* palette;
		byte* src;
		WADTEX* wadTex = NULL;

		int lastMipSize = (tex.nWidth / 8) * (tex.nHeight / 8);

		if (tex.nOffsets[0] <= 0) {

			bool foundInWad = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex.szName)) {
					foundInWad = true;

					wadTex = wads[k]->readTexture(tex.szName);
					palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + 2 - 40);
					src = wadTex->data;

					break;
				}
			}

			if (!foundInWad) {
				glTextures[i] = whiteTex;
				continue;
			}
		}
		else {
			palette = (COLOR3*)(map->textures + texOffset + tex.nOffsets[3] + lastMipSize + 2);
			src = map->textures + texOffset + tex.nOffsets[0];
		}

		COLOR3* imageData = new COLOR3[tex.nWidth * tex.nHeight];
		int sz = tex.nWidth * tex.nHeight;

		for (int k = 0; k < sz; k++) {
			imageData[k] = palette[src[k]];
		}

		if (wadTex) {
			delete wadTex;
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTextures[i] = new Texture(tex.nWidth, tex.nHeight, imageData);
	}

	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
}

void BspRenderer::loadLightmaps() {
	vector<LightmapNode*> atlases;
	vector<Texture*> atlasTextures;
	atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	memset(atlasTextures[0]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

	lightmaps = new LightmapInfo[map->faceCount];
	memset(lightmaps, 0, map->faceCount * sizeof(LightmapInfo));

	printf("Calculating lightmaps\n");
	qrad_init_globals(map);

	int lightmapCount = 0;
	int atlasId = 0;
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL))
			continue;

		int size[2];
		int dummy[2];
		int imins[2];
		int imaxs[2];
		GetFaceLightmapSize(i, size);
		GetFaceExtents(i, imins, imaxs);

		LightmapInfo& info = lightmaps[i];
		info.w = size[0];
		info.h = size[1];
		info.midTexU = (float)(size[0]) / 2.0f;
		info.midTexV = (float)(size[1]) / 2.0f;

		// TODO: float mins/maxs not needed?
		info.midPolyU = (imins[0] + imaxs[0]) * 16 / 2.0f;
		info.midPolyV = (imins[1] + imaxs[1]) * 16 / 2.0f;

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == 255)
				continue;

			// TODO: Try fitting in earlier atlases before using the latest one
			if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s])) {
				atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
				atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
				atlasId++;
				memset(atlasTextures[atlasId]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

				if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s])) {
					printf("Lightmap too big for atlas size!\n");
					continue;
				}
			}

			lightmapCount++;

			info.atlasId[s] = atlasId;

			// copy lightmap data into atlas
			int lightmapSz = info.w * info.h * sizeof(COLOR3);
			COLOR3* lightSrc = (COLOR3*)(map->lightdata + face.nLightmapOffset + s * lightmapSz);
			COLOR3* lightDst = (COLOR3*)(atlasTextures[atlasId]->data);
			for (int y = 0; y < info.h; y++) {
				for (int x = 0; x < info.w; x++) {
					int src = y * info.w + x;
					int dst = (info.y[s] + y) * LIGHTMAP_ATLAS_SIZE + info.x[s] + x;
					lightDst[dst] = lightSrc[src];
				}
			}
		}
	}

	glLightmapTextures = new Texture * [atlasTextures.size()];
	for (int i = 0; i < atlasTextures.size(); i++) {
		delete atlases[i];
		glLightmapTextures[i] = atlasTextures[i];
		glLightmapTextures[i]->upload();
	}

	lodepng_encode24_file("atlas.png", atlasTextures[0]->data, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE);
	printf("Fit %d lightmaps into %d atlases\n", lightmapCount, atlasId + 1);
}

void BspRenderer::preRenderFaces() {
	renderModels = new RenderModel[map->modelCount];

	for (int m = 0; m < map->modelCount; m++) {
		BSPMODEL& model = map->models[m];
		RenderModel& renderModel = renderModels[m];

		vector<RenderGroup> renderGroups;
		vector<vector<lightmapVert>> renderGroupVerts;

		for (int i = 0; i < model.nFaces; i++) {
			int faceIdx = model.iFirstFace + i;
			BSPFACE& face = map->faces[faceIdx];
			BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
			int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			LightmapInfo& lmap = lightmaps[faceIdx];

			lightmapVert* verts = new lightmapVert[face.nEdges];
			int vertCount = face.nEdges;
			Texture* texture = glTextures[texinfo.iMiptex];
			Texture* lightmapAtlas[MAXLIGHTMAPS];

			float tw = 1.0f / (float)tex.nWidth;
			float th = 1.0f / (float)tex.nHeight;

			float lw = (float)lmap.w / (float)LIGHTMAP_ATLAS_SIZE;
			float lh = (float)lmap.h / (float)LIGHTMAP_ATLAS_SIZE;

			bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
			bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				lightmapAtlas[s] = glLightmapTextures[lmap.atlasId[s]];
			}

			if (isSpecial) {
				lightmapAtlas[0] = whiteTex;
			}

			float opacity = isSpecial ? 0.5f : 1.0f;

			for (int e = 0; e < face.nEdges; e++) {
				int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
				BSPEDGE& edge = map->edges[abs(edgeIdx)];
				int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

				vec3& vert = map->verts[vertIdx];
				verts[e].x = vert.x;
				verts[e].y = vert.z;
				verts[e].z = -vert.y;

				// texture coords
				float fU = dotProduct(texinfo.vS, vert) + texinfo.shiftS;
				float fV = dotProduct(texinfo.vT, vert) + texinfo.shiftT;
				verts[e].u = fU * tw;
				verts[e].v = fV * th;
				verts[e].opacity = isSpecial ? 0.5f : 1.0f;

				// lightmap texture coords
				if (hasLighting) {
					float fLightMapU = lmap.midTexU + (fU - lmap.midPolyU) / 16.0f;
					float fLightMapV = lmap.midTexV + (fV - lmap.midPolyV) / 16.0f;

					float uu = (fLightMapU / (float)lmap.w) * lw;
					float vv = (fLightMapV / (float)lmap.h) * lh;

					float pixelStep = 1.0f / (float)LIGHTMAP_ATLAS_SIZE;

					for (int s = 0; s < MAXLIGHTMAPS; s++) {
						verts[e].luv[s][0] = uu + lmap.x[s] * pixelStep;
						verts[e].luv[s][1] = vv + lmap.y[s] * pixelStep;
					}
				}
				// set lightmap scales
				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][2] = (hasLighting && face.nStyles[s] != 255) ? 1.0f : 0.0f;
					if (isSpecial && s == 0) {
						verts[e].luv[s][2] = 1.0f;
					}
				}
			}


			// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
			int newCount = face.nEdges + max(0, face.nEdges - 3) * 2;
			lightmapVert* newVerts = new lightmapVert[newCount];

			int idx = 0;
			for (int k = 2; k < face.nEdges; k++) {
				newVerts[idx++] = verts[0];
				newVerts[idx++] = verts[k - 1];
				newVerts[idx++] = verts[k];
			}

			delete[] verts;
			verts = newVerts;
			vertCount = newCount;

			// add face to a render group (faces that share that same textures and opacity flag)
			{
				bool isTransparent = opacity < 1.0f;
				int groupIdx = -1;
				for (int k = 0; k < renderGroups.size(); k++) {
					if (renderGroups[k].texture == glTextures[texinfo.iMiptex] && renderGroups[k].transparent == isTransparent) {
						bool allMatch = true;
						for (int s = 0; s < MAXLIGHTMAPS; s++) {
							if (renderGroups[k].lightmapAtlas[s] != lightmapAtlas[s]) {
								allMatch = false;
								break;
							};
						}
						if (allMatch) {
							groupIdx = k;
							break;
						}
					}
				}

				if (groupIdx == -1) {
					RenderGroup newGroup = RenderGroup();
					newGroup.vertCount = 0;
					newGroup.verts = NULL;
					newGroup.transparent = isTransparent;
					newGroup.texture = glTextures[texinfo.iMiptex];
					for (int s = 0; s < MAXLIGHTMAPS; s++) {
						newGroup.lightmapAtlas[s] = lightmapAtlas[s];
					}
					renderGroups.push_back(newGroup);
					renderGroupVerts.push_back(vector<lightmapVert>());
					groupIdx = renderGroups.size() - 1;
				}

				for (int k = 0; k < vertCount; k++)
					renderGroupVerts[groupIdx].push_back(verts[k]);
			}
		}

		renderModel.renderGroups = new RenderGroup[renderGroups.size()];
		renderModel.groupCount = renderGroups.size();

		for (int i = 0; i < renderGroups.size(); i++) {
			renderGroups[i].verts = new lightmapVert[renderGroupVerts[i].size()];
			renderGroups[i].vertCount = renderGroupVerts[i].size();
			memcpy(renderGroups[i].verts, &renderGroupVerts[i][0], renderGroups[i].vertCount * sizeof(lightmapVert));

			renderGroups[i].buffer = new VertexBuffer(pipeline, 0);
			renderGroups[i].buffer->addAttribute(TEX_2F, "vTex");
			renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
			renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
			renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
			renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
			renderGroups[i].buffer->addAttribute(1, GL_FLOAT, 0, "vOpacity");
			renderGroups[i].buffer->addAttribute(POS_3F, "vPosition");
			renderGroups[i].buffer->setData(renderGroups[i].verts, renderGroups[i].vertCount);
			renderGroups[i].buffer->upload();

			renderModel.renderGroups[i] = renderGroups[i];
		}

		printf("Added %d render groups for model %d\n", renderModel.groupCount, m);
	}
}

void BspRenderer::preRenderEnts() {
	renderEnts = new RenderEnt[map->ents.size()];

	for (int i = 0; i < map->ents.size(); i++) {
		Entity* ent = map->ents[i];

		renderEnts[i].modelIdx = ent->getBspModelIdx();
		renderEnts[i].modelMat.loadIdentity();

		if (ent->hasKey("origin")) {
			vec3 origin = Keyvalue("", ent->keyvalues["origin"]).getVector();
			renderEnts[i].modelMat.translate(origin.x, origin.z, -origin.y);
		}
	}
}

BspRenderer::~BspRenderer() {
	for (int i = 0; i < map->textureCount; i++) {
		delete glTextures[i];
	}
	delete[] glTextures;

	// TODO: more stuff to delete
}

void BspRenderer::render() {
	BSPMODEL& world = map->models[0];	

	bool fastMode = true;

	if (fastMode) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		for (int pass = 0; pass < 2; pass++) {
			bool drawTransparentFaces = pass == 1;

			drawModel(0, drawTransparentFaces);

			for (int i = 0, sz = map->ents.size(); i < sz; i++) {
				if (renderEnts[i].modelIdx >= 0) {
					RenderGroup& rgroup = renderModels[renderEnts[i].modelIdx].renderGroups[i];

					pipeline->pushMatrix(MAT_MODEL);
					pipeline->modelMat = &renderEnts[i].modelMat;
					pipeline->updateMatrixes();

					drawModel(renderEnts[i].modelIdx, drawTransparentFaces);

					pipeline->popMatrix(MAT_MODEL);
				}
			}
		}
	}	
}


void BspRenderer::drawModel(int modelIdx, bool transparent) {
	for (int i = 0; i < renderModels[modelIdx].groupCount; i++) {
		RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

		if (rgroup.transparent != transparent)
			continue;

		glActiveTexture(GL_TEXTURE0);
		rgroup.texture->bind();

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			glActiveTexture(GL_TEXTURE1 + s);
			rgroup.lightmapAtlas[s]->bind();
		}

		rgroup.buffer->draw(GL_TRIANGLES);
	}
}