#include "util.h"
#include "BspMerger.h"
#include <string>
#include <algorithm>
#include <iostream>
#include "CommandLine.h"
#include "remap.h"

// super todo:
// game crashes randomly, usually a few minutes after not focused on the game (maybe from edit+restart?)

// minor todo:
// trigger_changesky for series maps with different skies
// warn about game_playerjoin and other special names
// fix spawners for things with custom keyvalues (apache, osprey, etc.)
// dump model info for the rest of the data types
// use min lightmap size for faces with bad extents? Saves ~3kb per face
// automatically remove clipnodes/faces from things that don't need them
// - no hull2 if map has no large monsters or pushables
// - no clipnodes if it's func_illusionary or marked as nonsolid
// - no faces if ent is invisible (trigger_once). Maybe this breaks them tho? 
// check if models with origins have any special bsp model values, maybe splitting isn't needed
// delete all frames from unused animated textures

// refactoring:
// save data structure pointers+sizes in Bsp class instead of copy-pasting them everywhere
// stop mixing printf+cout
// create progress-printing class instead of the methods used now


// Ideas for commands:
// optimize:
//		- merges redundant submodels (copy-pasting a picard coin all over the map)
//		- conditionally remove hull2 or func_illusionary clipnodes
// copymodel:
//		- copies a model from the source map into the target map (for adding new perfectly shaped brush ents)
// addbox:
//		- creates a new box-shaped brush model (faster than copymodel if you don't need anything fancy)
// extract:
//		- extracts an isolated room from the BSP
// decompile:
//      - to RMF. Try creating brushes from convex face connections?
// export:
//      - export BSP models to MDL models.
// clip:
//		- replace the clipnodes of a model with a simple bounding box.

// Notes:
// Removing HULL 0 from any model crashes when shooting unless it's EF_NODRAW or renderamt=0
// Removing HULL 0 from solid model crashes game when standing on it


const char* version_string = "bspguy v2 WIP (May 2020)";

bool g_verbose = false;

int test() {
	/*
	Bsp test("op4/of1a4.bsp");
	test.validate();
	//test.strip_clipping_hull(2);
	test.remove_useless_clipnodes();
	test.move(vec3(0, 64, 0));
	test.write("yabma_move.bsp");
	test.write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
	test.print_info(false, 0, 0);
	return 0;
	*/

	vector<Bsp*> maps;
	
	/*
	for (int i = 1; i < 22; i++) {
		Bsp* map = new Bsp("2nd/saving_the_2nd_amendment" + (i > 1 ? to_string(i) : "") + ".bsp");
		map->strip_clipping_hull(2);
		maps.push_back(map);
	}
	*/

	//maps.push_back(new Bsp("echoes/echoes01.bsp"));
	//maps.push_back(new Bsp("echoes/echoes01a.bsp"));
	//maps.push_back(new Bsp("echoes/echoes02.bsp"));

	//maps.push_back(new Bsp("echoes03.bsp"));
	//maps.push_back(new Bsp("echoes04.bsp"));
	//maps.push_back(new Bsp("echoes05.bsp"));

	//maps.push_back(new Bsp("echoes06.bsp"));
	//maps.push_back(new Bsp("echoes07.bsp"));

	//maps.push_back(new Bsp("echoes09.bsp"));
	//maps.push_back(new Bsp("echoes09a.bsp"));

	//maps.push_back(new Bsp("echoes09b.bsp"));
	//maps.push_back(new Bsp("echoes10.bsp"));

	//maps.push_back(new Bsp("echoes12.bsp"));
	//maps.push_back(new Bsp("echoes13.bsp"));

	//maps.push_back(new Bsp("merge1.bsp"));
	//maps.push_back(new Bsp("echoes/echoes14.bsp"));
	//maps.push_back(new Bsp("echoes/echoes14b.bsp"));

	maps.push_back(new Bsp("merge0.bsp"));
	maps.push_back(new Bsp("merge1.bsp"));

	//maps.push_back(new Bsp("op4/of1a1.bsp"));
	//maps.push_back(new Bsp("op4/of1a2.bsp"));
	//maps.push_back(new Bsp("op4/of1a3.bsp"));
	//maps.push_back(new Bsp("op4/of1a4.bsp"));

	for (int i = 0; i < maps.size(); i++) {
		if (!maps[i]->valid) {
			return 1;
		}
		printf("Process %s\n", maps[i]->name.c_str());
		if (!maps[i]->validate()) {
			printf("");
		}
		maps[i]->strip_clipping_hull(2);
		maps[i]->resize_hull2_ents();
		maps[i]->delete_unused_hulls();
		//maps[i]->remove_unused_model_structures();

		//maps[i]->print_info(true, 10, SORT_CLIPNODES);
	}

	BspMerger merger;
	Bsp* result = merger.merge(maps, vec3(1, 1, 1), false, true);
	printf("\n");
	if (result != NULL) {
		result->write("yabma_move.bsp");
		result->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		result->print_info(false, 0, false);
	}
	return 0;
}

int merge_maps(CommandLine& cli) {
	vector<string> input_maps = cli.getOptionList("-maps");

	if (input_maps.size() < 2) {
		cout << "ERROR: at least 2 input maps are required\n";
		return 1;
	}

	vector<Bsp*> maps;

	for (int i = 0; i < input_maps.size(); i++) {
		Bsp* map = new Bsp(input_maps[i]);
		if (!map->valid)
			return 1;
		maps.push_back(map);
	}

	bool shouldPreprocess = cli.hasOption("-nohull2") || !cli.hasOption("-safe");

	if (shouldPreprocess) {
		printf("Pre-processing maps:\n");
	}

	if (cli.hasOption("-nohull2")) {
		for (int i = 0; i < maps.size(); i++) {
			int removedClipnodes = maps[i]->strip_clipping_hull(2);
			int resizedEnts = maps[i]->resize_hull2_ents();
			maps[i]->remove_unused_model_structures();
			printf("    Deleted hull 2 (%d clipnodes) in %s\n", removedClipnodes, maps[i]->name.c_str());
			if (resizedEnts)
				printf("    Resized %d large monsters/pushables in %s\n", resizedEnts, maps[i]->name.c_str());
		}
		
	}

	if (!cli.hasOption("-safe")) {
		for (int i = 0; i < maps.size(); i++) {
			if (!cli.hasOption("-nohull2") && !maps[i]->has_hull2_ents()) {
				int removedClipnodes = maps[i]->strip_clipping_hull(2);
				printf("    Deleted hull 2 (%d clipnodes) in %s\n", removedClipnodes, maps[i]->name.c_str());
			}
			int deletedHulls = maps[i]->delete_unused_hulls();
			printf("    Deleted %d unused model hulls in %s\n", deletedHulls, maps[i]->name.c_str());
		}
	}

	if (shouldPreprocess) {
		printf("\n");
	}
	
	vec3 gap = cli.hasOption("-gap") ? cli.getOptionVector("-gap") : vec3(0,0,0);

	BspMerger merger;
	Bsp* result = merger.merge(maps, gap, cli.hasOption("-noripent"), cli.hasOption("-nohull2"));

	printf("\n");
	if (result->isValid()) result->write(cli.hasOption("-o") ? cli.getOption("-o") : cli.bspfile);
	printf("\n");
	result->print_info(false, 0, 0);

	for (int i = 0; i < maps.size(); i++) {
		delete maps[i];
	}

	return 0;
}

int print_info(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	bool limitMode = false;
	int listLength = 10;
	int sortMode = SORT_CLIPNODES;

	if (cli.hasOption("-limit")) {
		string limitName = cli.getOption("-limit");
			
		limitMode = true;
		if (limitName == "clipnodes") {
			sortMode = SORT_CLIPNODES;
		}
		else if (limitName == "nodes") {
			sortMode = SORT_NODES;
		}
		else if (limitName == "faces") {
			sortMode = SORT_FACES;
		}
		else if (limitName == "vertexes") {
			sortMode = SORT_VERTS;
		}
		else {
			cout << "ERROR: invalid limit name: " << limitName << endl;
			return 0;
		}
	}
	if (cli.hasOption("-all")) {
		listLength = 32768; // should be more than enough
	}

	map->print_info(limitMode, listLength, sortMode);

	delete map;

	return 0;
}

void print_delete_stats(STRUCTCOUNT& stats) {
	if (stats.models) printf("    Deleted %d models\n", stats.models);
	if (stats.planes) printf("    Deleted %d planes\n", stats.planes);
	if (stats.verts) printf("    Deleted %d vertexes\n", stats.verts);
	if (stats.nodes) printf("    Deleted %d nodes\n", stats.nodes);
	if (stats.texInfos) printf("    Deleted %d texinfos\n", stats.texInfos);
	if (stats.faces) printf("    Deleted %d faces\n", stats.faces);
	if (stats.clipnodes) printf("    Deleted %d clipnodes\n", stats.clipnodes);
	if (stats.leaves) printf("    Deleted %d leaves\n", stats.leaves);
	if (stats.markSurfs) printf("    Deleted %d marksurfaces\n", stats.markSurfs);
	if (stats.surfEdges) printf("    Deleted %d surfedges\n", stats.surfEdges);
	if (stats.edges) printf("    Deleted %d edges\n", stats.edges);
	if (stats.textures) printf("    Deleted %d textures\n", stats.textures);
	if (stats.lightdata) printf("    Deleted %.2f KB of lightmap data\n", stats.lightdata / 1024.0f);
	if (stats.visdata) printf("    Deleted %.2f KB VIS data\n", stats.visdata / 1024.0f);
}

// remove unused data before modifying anything to avoid misleading results
void remove_unused_data(Bsp* map) {
	STRUCTCOUNT removed =  map->remove_unused_model_structures();

	if (!removed.allZero()) {
		printf("Deleting unused data:\n");
		print_delete_stats(removed);
		printf("\n");
	}
}

int noclip(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	int model = -1;
	int hull = -1;

	if (cli.hasOption("-hull")) {
		hull = cli.getOptionInt("-hull");

		if (hull < 0 || hull >= MAX_MAP_HULLS) {
			cout << "ERROR: hull number must be 1-3\n";
			return 1;
		}
	}

	remove_unused_data(map);

	STRUCTCOUNT removed;
	memset(&removed, 0, sizeof(removed));

	if (cli.hasOption("-model")) {
		model = cli.getOptionInt("-model");

		int modelCount = map->header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

		if (model < 0) {
			cout << "ERROR: model number must be 0 or greater\n";
			return 1;
		}
		if (model >= modelCount) {
			printf("ERROR: there are only %d models in this map\n", modelCount);
			return 1;
		}

		if (hull != -1) {
			if (hull == 0) {
				printf("Deleting HULL 0 from model %d:\n", model);
				removed = map->delete_model_faces(model);
			}
			else {
				printf("Deleting HULL %d from model %d:\n", hull, model);
				removed.clipnodes += map->strip_clipping_hull(hull, model, false);
			}

		}
		else {
			printf("Deleting HULL 1, 2, and 3 from model %d:\n", model);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				removed.clipnodes += map->strip_clipping_hull(i, model, false);
			}
		}
	}
	else {
		if (hull == 0) {
			printf("HULL 0 can't be stripped globally. The entire map would be invisible!\n");
			return 0;
		}

		if (hull != -1) {
			printf("Deleting HULL %d:\n", hull);
			removed.clipnodes += map->strip_clipping_hull(hull);
		}
		else {
			printf("Deleting HULL 1, 2, and 3:\n", hull);
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				removed.clipnodes += map->strip_clipping_hull(i);
			}
		}
	}

	removed.add(map->remove_unused_model_structures());

	if (!removed.allZero())
		print_delete_stats(removed);
	else
		printf("    Model hull(s) was previously deleted.");
	printf("\n");

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;

	return 0;
}

int deleteCmd(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	remove_unused_data(map);

	if (cli.hasOption("-model")) {
		int modelIdx = cli.getOptionInt("-model");

		printf("Deleting model %d:\n", modelIdx);
		map->delete_model(modelIdx);
		map->update_ent_lump();
		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
			print_delete_stats(removed);
		printf("\n");
	}

	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

int transform(CommandLine& cli) {
	Bsp* map = new Bsp(cli.bspfile);
	if (!map->valid)
		return 1;

	vec3 move;

	if (cli.hasOptionVector("-move")) {
		move = cli.getOptionVector("-move");

		printf("Applying offset (%.2f, %.2f, %.2f)\n",
			move.x, move.y, move.z);

		map->move(move);
	}
	else {
		printf("ERROR: at least one transformation option is required\n");
		return 1;
	}
	
	if (map->isValid()) map->write(cli.hasOption("-o") ? cli.getOption("-o") : map->path);
	printf("\n");

	map->print_info(false, 0, 0);

	delete map;
}

void print_help(string command) {
	if (command == "merge") {
		cout <<
			"merge - Merges two or more maps together\n\n"

			"Usage:   bspguy merge <mapname> -maps \"map1, map2, ... mapN\" [options]\n"
			"Example: bspguy merge merged.bsp -maps \"svencoop1, svencoop2\"\n"

			"\n[Options]\n"
			"  -safe        : By default, unused model hulls are removed before merging.\n"
			"                 This can be risky and crash the game if assumptions about\n"
			"                 entity visibility/solidity are wrong. This flag prevents\n"
			"                 any unsafe hull removals.\n"
			"  -nohull2     : Forces removal of hull 2 from each map before merging.\n"
			"                 Large monsters and pushables will be resized if needed.\n"
			"  -noripent    : By default, the input maps are assumed to be part of a series.\n"
			"                 Level changes and other things are updated so that the merged\n"
			"                 maps can be played one after another. This flag prevents any\n"
			"                 entity edits from being made (except for origins).\n"
			"  -gap \"X,Y,Z\" : Amount of extra space to add between each map\n"
			"  -v           : Verbose console output.\n"
			;
	}
	else if (command == "info") {
		cout <<
			"info - Show BSP data summary\n\n"

			"Usage:   bspguy info <mapname> [options]\n"
			"Example: bspguy info svencoop1.bsp -limit clipnodes -all\n"

			"\n[Options]\n"
			"  -limit <name> : List the models contributing most to the named limit.\n"
			"                  <name> can be one of: [clipnodes, nodes, faces, vertexes]\n"
			"  -all          : Show the full list of models when using -limit.\n"
			;
	}
	else if (command == "noclip") {
		cout <<
			"noclip - Delete some clipnodes from the BSP\n\n"

			"Usage:   bspguy noclip <mapname> [options]\n"
			"Example: bspguy noclip svencoop1.bsp -hull 2\n"

			"\n[Options]\n"
			"  -model #  : Model to strip collision from. By default, all models are stripped.\n"
			"  -hull #   : Collision hull to strip (0-3). By default, hulls 1-3 are stripped.\n"
			"              0 = Point-size collision and visible surfaces\n"
			"              1 = Human-sized monsters and standing players\n"
			"              2 = Large monsters and pushables\n"
			"              3 = Small monsters, crouching players, and melee attacks\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else if (command == "delete") {
		cout <<
			"delete - Delete BSP models.\n\n"

			"Usage:   bspguy delete <mapname> [options]\n"
			"Example: bspguy delete svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #  : Model to delete. Entities that reference the deleted\n"
			"              model will be updated to use error.mdl instead.\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else if (command == "transform") {
		cout <<
			"transform - Apply 3D transformations\n\n"

			"Usage:   bspguy transform <mapname> [options]\n"
			"Example: bspguy transform svencoop1.bsp -move \"0,0,1024\"\n"

			"\n[Options]\n"
			"  -move \"X,Y,Z\" : Units to move the map on each axis.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
			;
	}
	else {
		cout << version_string << endl << endl <<
			"This tool modifies Sven Co-op BSPs without having to decompile them.\n\n"
			"Usage: bspguy <command> <mapname> [options]\n"

			"\n<Commands>\n"
			"  info      : Show BSP data summary\n"
			"  merge     : Merges two or more maps together\n"
			"  noclip    : Delete some clipnodes/nodes from the BSP\n"
			"  delete    : Delete BSP models\n"
			"  transform : Apply 3D transformations to the BSP\n"

			"\nRun 'bspguy <command> help' to read about a specific command.\n"
			;
	}
}

int main(int argc, char* argv[])
{
	// return test();

	CommandLine cli(argc, argv);

	if (cli.askingForHelp) {
		print_help(cli.command);
		return 0;
	}

	if (cli.command == "version" || cli.command == "--version" || cli.command == "-version" || cli.command == "-v") {
		printf(version_string);
		return 0;
	}

	if (cli.bspfile.empty()) {
		cout << "ERROR: no map specified\n"; return 1;
	}

	if (cli.hasOption("-v")) {
		g_verbose = true;
	}

	if (cli.command == "info") {
		return print_info(cli);
	}
	else if (cli.command == "noclip") {
		return noclip(cli);
	}
	else if (cli.command == "delete") {
		return deleteCmd(cli);
	}
	else if (cli.command == "transform") {
		return transform(cli);
	}
	else if (cli.command == "merge") {
		return merge_maps(cli);
	}
	else {
		cout << "unrecognized command: " << cli.command << endl;
	}

	return 0;
}

