// mod.cpp: define the "vg mod" subcommand, which modifies vg graphs

#include <omp.h>
#include <unistd.h>
#include <getopt.h>

#include <string>
#include <vector>
#include <regex>

#include "subcommand.hpp"

#include "../vg.hpp"
#include "../stream.hpp"
#include "../utility.hpp"

using namespace std;
using namespace vg;
using namespace vg::subcommand;

void help_mod(char** argv) {
    cerr << "usage: " << argv[0] << " mod [options] <graph.vg> >[mod.vg]" << endl
         << "Modifies graph, outputs modified on stdout." << endl
         << endl
         << "options:" << endl
         << "    -i, --include-aln FILE  merge the paths implied by alignments into the graph" << endl
         << "    -q, --include-loci FILE merge all alleles in loci into the graph" << endl
         << "    -Q, --include-gt FILE   merge only the alleles in called genotypes into the graph" << endl
         << "    -Z, --translation FILE  write the translation generated by editing with -i to FILE" << endl
         << "    -P, --label-paths       don't edit with -i alignments, just use them for labeling the graph" << endl
         << "    -c, --compact-ids       should we sort and compact the id space? (default false)" << endl
         << "    -C, --compact-ranks     compact mapping ranks in paths" << endl
         << "    -z, --sort              sort the graph using an approximate topological sort" << endl
         << "    -b, --break-cycles      use an approximate topological sort to break cycles in the graph" << endl
         << "    -n, --normalize         normalize the graph so that edges are always non-redundant" << endl
         << "                            (nodes have unique starting and ending bases relative to neighbors," << endl
         << "                            and edges that do not introduce new paths are removed and neighboring" << endl
         << "                            nodes are merged)" << endl
         << "    -U, --until-normal N    iterate normalization until convergence, or at most N times" << endl
         << "    -E, --unreverse-edges   flip doubly-reversing edges so that they are represented on the" << endl
         << "                            forward strand of the graph" << endl
         << "    -s, --simplify          remove redundancy from the graph that will not change its path space" << endl
         << "    -T, --strong-connect    outputs the strongly-connected components of the graph" << endl
         << "    -d, --dagify-step N     copy strongly connected components of the graph N times, forwarding" << endl
         << "                            edges from old to new copies to convert the graph into a DAG" << endl
         << "    -w, --dagify-to N       copy strongly connected components of the graph forwarding" << endl
         << "                            edges from old to new copies to convert the graph into a DAG" << endl
         << "                            until the shortest path through each SCC is N bases long" << endl
         << "    -L, --dagify-len-max N  stop a dagification step if the unrolling component has this much sequence" << endl
         << "    -f, --unfold N          represent inversions accesible up to N from the forward" << endl
         << "                            component of the graph" << endl
         << "    -O, --orient-forward    orient the nodes in the graph forward" << endl
         << "    -D, --drop-paths        remove the paths of the graph" << endl
         << "    -r, --retain-path NAME  remove any path not specified for retention" << endl
         << "    -I, --retain-complement keep only paths NOT specified with -r" << endl
         << "    -k, --keep-path NAME    keep only nodes and edges in the path" << endl
         << "    -N, --remove-non-path   keep only nodes and edges which are part of paths" << endl
         << "    -o, --remove-orphans    remove orphan edges from graph (edge specified but node missing)" << endl
         << "    -R, --remove-null       removes nodes that have no sequence, forwarding their edges" << endl
         << "    -g, --subgraph ID       gets the subgraph rooted at node ID, multiple allowed" << endl
         << "    -x, --context N         steps the subgraph out by N steps (default: 1)" << endl
         << "    -p, --prune-complex     remove nodes that are reached by paths of --length which" << endl
         << "                            cross more than --edge-max edges" << endl
         << "    -S, --prune-subgraphs   remove subgraphs which are shorter than --length" << endl
         << "    -l, --length N          for pruning complex regions and short subgraphs" << endl
         << "    -X, --chop N            chop nodes in the graph so they are not more than N bp long" << endl
         << "    -u, --unchop            where two nodes are only connected to each other and by one edge" << endl
         << "                            replace the pair with a single node that is the concatenation of their labels" << endl
         << "    -K, --kill-labels       delete the labels from the graph, resulting in empty nodes" << endl
         << "    -e, --edge-max N        only consider paths which make edge choices at <= this many points" << endl
         << "    -m, --markers           join all head and tails nodes to marker nodes" << endl
         << "                            ('###' starts and '$$$' ends) of --length, for debugging" << endl
         << "    -F, --force-path-match  sets path edits explicitly equal to the nodes they traverse" << endl
         << "    -y, --destroy-node ID   remove node with given id" << endl
         << "    -B, --bluntify          bluntify the graph, making nodes for duplicated sequences in overlaps" << endl
         << "    -a, --cactus            convert to cactus graph representation" << endl
         << "    -v, --sample-vcf FILE   for a graph with allele paths, compute the sample graph from the given VCF" << endl
         << "    -t, --threads N         for tasks that can be done in parallel, use this many threads" << endl;
}

int main_mod(int argc, char** argv) {

    if (argc == 2) {
        help_mod(argv);
        return 1;
    }

    string path_name;
    bool remove_orphans = false;
    string aln_file;
    string loci_file;
    bool called_genotypes_only = false;
    bool label_paths = false;
    bool compact_ids = false;
    bool prune_complex = false;
    int path_length = 0;
    int edge_max = 0;
    int chop_to = 0;
    bool add_start_and_end_markers = false;
    bool prune_subgraphs = false;
    bool kill_labels = false;
    bool simplify_graph = false;
    bool unchop = false;
    bool normalize_graph = false;
    bool sort_graph = false;
    bool remove_non_path = false;
    bool compact_ranks = false;
    bool drop_paths = false;
    bool force_path_match = false;
    set<string> paths_to_retain;
    bool retain_complement = false;
    vector<int64_t> root_nodes;
    int32_t context_steps;
    bool remove_null;
    bool strong_connect = false;
    uint32_t unfold_to = 0;
    bool break_cycles = false;
    uint32_t dagify_steps = 0;
    uint32_t dagify_to = 0;
    uint32_t dagify_component_length_max = 0;
    bool orient_forward = false;
    int64_t destroy_node_id = 0;
    bool bluntify = false;
    int until_normal_iter = 0;
    string translation_file;
    bool flip_doubly_reversed_edges = false;
    bool cactus = false;
    string vcf_filename;

    int c;
    optind = 2; // force optind past command positional argument
    while (true) {
        static struct option long_options[] =

        {
            {"help", no_argument, 0, 'h'},
            {"include-aln", required_argument, 0, 'i'},
            {"include-loci", required_argument, 0, 'q'},
            {"include-gt", required_argument, 0, 'Q'},
            {"compact-ids", no_argument, 0, 'c'},
            {"compact-ranks", no_argument, 0, 'C'},
            {"drop-paths", no_argument, 0, 'D'},
            {"keep-path", required_argument, 0, 'k'},
            {"remove-orphans", no_argument, 0, 'o'},
            {"prune-complex", no_argument, 0, 'p'},
            {"prune-subgraphs", no_argument, 0, 'S'},
            {"length", required_argument, 0, 'l'},
            {"edge-max", required_argument, 0, 'e'},
            {"chop", required_argument, 0, 'X'},
            {"kill-labels", no_argument, 0, 'K'},
            {"markers", no_argument, 0, 'm'},
            {"threads", no_argument, 0, 't'},
            {"label-paths", no_argument, 0, 'P'},
            {"simplify", no_argument, 0, 's'},
            {"unchop", no_argument, 0, 'u'},
            {"normalize", no_argument, 0, 'n'},
            {"until-normal", required_argument, 0, 'U'},
            {"sort", no_argument, 0, 'z'},
            {"remove-non-path", no_argument, 0, 'N'},
            {"orient-forward", no_argument, 0, 'O'},
            {"unfold", required_argument, 0, 'f'},
            {"force-path-match", no_argument, 0, 'F'},
            {"retain-path", required_argument, 0, 'r'},
            {"subgraph", required_argument, 0, 'g'},
            {"context", required_argument, 0, 'x'},
            {"remove-null", no_argument, 0, 'R'},
            {"strong-connect", no_argument, 0, 'T'},
            {"dagify-steps", required_argument, 0, 'd'},
            {"dagify-to", required_argument, 0, 'w'},
            {"dagify-len-max", required_argument, 0, 'L'},
            {"bluntify", no_argument, 0, 'B'},
            {"break-cycles", no_argument, 0, 'b'},
            {"orient-forward", no_argument, 0, 'O'},
            {"destroy-node", required_argument, 0, 'y'},
            {"translation", required_argument, 0, 'Z'},
            {"unreverse-edges", required_argument, 0, 'E'},
            {"cactus", no_argument, 0, 'a'},
            {"sample-vcf", required_argument, 0, 'v'},
            {0, 0, 0, 0}
        };

        int option_index = 0;
        c = getopt_long (argc, argv, "hk:oi:q:Q:cpl:e:mt:SX:KPsunzNf:CDFr:Ig:x:RTU:Bbd:Ow:L:y:Z:Eav:",
                long_options, &option_index);


        // Detect the end of the options.
        if (c == -1)
            break;

        switch (c)
        {

        case 'i':
            aln_file = optarg;
            break;

        case 'q':
            loci_file = optarg;
            break;

        case 'Q':
            loci_file = optarg;
            called_genotypes_only = true;
            break;

        case 'Z':
            translation_file = optarg;
            break;

        case 'c':
            compact_ids = true;
            break;

        case 'C':
            compact_ranks = true;
            break;

        case 'k':
            path_name = optarg;
            break;

        case 'r':
            paths_to_retain.insert(optarg);
            break;
            
        case 'I':
            retain_complement = true;
            break;

        case 'o':
            remove_orphans = true;
            break;

        case 'p':
            prune_complex = true;
            break;

        case 'S':
            prune_subgraphs = true;
            break;

        case 'l':
            path_length = atoi(optarg);
            break;

        case 'X':
            chop_to = atoi(optarg);
            break;

        case 'u':
            unchop = true;
            break;

        case 'E':
            flip_doubly_reversed_edges = true;
            break;

        case 'K':
            kill_labels = true;
            break;

        case 'e':
            edge_max = atoi(optarg);
            break;

        case 'm':
            add_start_and_end_markers = true;
            break;

        case 't':
            omp_set_num_threads(atoi(optarg));
            break;

        case 'f':
            unfold_to = atoi(optarg);
            break;

        case 'O':
            orient_forward = true;
            break;

        case 'F':
            force_path_match = true;
            break;

        case 'P':
            label_paths = true;
            break;

        case 'D':
            drop_paths = true;
            break;

        case 's':
            simplify_graph = true;
            break;

        case 'n':
            normalize_graph = true;
            break;

        case 'N':
            remove_non_path = true;
            break;

        case 'T':
            strong_connect = true;
            break;

        case 'U':
            until_normal_iter = atoi(optarg);
            break;

        case 'd':
            dagify_steps = atoi(optarg);
            break;

        case 'w':
            dagify_to = atoi(optarg);
            break;

        case 'L':
            dagify_component_length_max = atoi(optarg);
            break;

        case 'B':
            bluntify = true;
            break;

        case 'z':
            sort_graph = true;
            break;

        case 'b':
            break_cycles = true;
            break;

        case 'g':
            root_nodes.push_back(atoi(optarg));
            break;

        case 'x':
            context_steps = atoi(optarg);
            break;

        case 'R':
            remove_null = true;
            break;

        case 'y':
            destroy_node_id = atoi(optarg);
            break;

        case 'a':
            cactus = true;
            break;

        case 'v':
            vcf_filename = optarg;
            break;

        case 'h':
        case '?':
            help_mod(argv);
            exit(1);
            break;

        default:
            abort ();
        }
    }

    VG* graph;
    get_input_file(optind, argc, argv, [&](istream& in) {
        graph = new VG(in);
    });
    
    if (retain_complement) {
        // Compute the actual paths to retain
        set<string> complement;
        graph->paths.for_each_name([&](const string& name) {
            if (!paths_to_retain.count(name)) {
                // Complement the set the user specified by putting in all the
                // paths they didn't mention.
                complement.insert(name);
            }
        });
        
        // Retain the complement of what we were asking for.
        paths_to_retain = complement;
    }

    if (!vcf_filename.empty()) {
        // We need to throw out the parts of the graph that are on alt paths,
        // but not on alt paths for alts used by the first sample in the VCF.

        // This is matched against the entire path name string to detect alt
        // paths.
        regex is_alt("_alt_.+_[0-9]+");

        // This holds the VCF file we read the variants from. It needs to be the
        // same one used to construct the graph.
        vcflib::VariantCallFile variant_file;
        variant_file.open(vcf_filename);
        if (!variant_file.is_open()) {
            cerr << "error:[vg mod] could not open" << vcf_filename << endl;
            return 1;
        }

        // Now go through and prune down the varaints.

        // How many phases are there?
        size_t num_samples = variant_file.sampleNames.size();
        // TODO: we can only handle single-sample VCFs
        assert(num_samples == 1);

        // This will hold the IDs of all nodes visited by alt paths that aren't used.
        set<vg::id_t> alt_path_ids;

        graph->paths.for_each_name([&](const string& alt_path_name) {
            // For every path name in the graph

            if(regex_match(alt_path_name, is_alt)) {
                // If it's an alt path

                for(auto& mapping : graph->paths.get_path(alt_path_name)) {
                    // Mark all nodes that are part of it as on alt paths
                    alt_path_ids.insert(mapping.position().node_id());
                }

            }
        });

        // We also have a function to handle each variant as it comes in.
        auto handle_variant = [&](vcflib::Variant& variant) {
            // So we have a variant

            if(variant.alleles.size() < 2) {
                // Skip non-variable variants.
                return;
            }

            // Grab its id, or make one by hashing stuff if it doesn't
            // have an ID.
            string var_name = make_variant_id(variant);

            if(!graph->paths.has_path("_alt_" + var_name + "_0")) {
                // There isn't a reference alt path for this variant. Someone messed up.
                cerr << variant << endl;
                throw runtime_error("Reference alt for " + var_name + " not in graph!");
            }

            // For now always work on sample 0. TODO: let the user specify a
            // name and find it.
            int sample_number = 0;

            // What sample is it?
            string& sample_name = variant_file.sampleNames[sample_number];

            // Parse it out and see if it's phased.
            string genotype = variant.getGenotype(sample_name);

            // Tokenize into allele numbers
            // The token iterator can't hold the regex
            regex allele_separator("[|/]");
            for (sregex_token_iterator it(genotype.begin(), genotype.end(), allele_separator, -1);
                it != sregex_token_iterator(); ++it) {
                // For every token separated by / or |
                int allele_number;
                if(it->str() == ".") {
                    // Unknown; pretend it's ref for the purposes of making a
                    // sample graph.
                    allele_number = 0;
                } else {
                    // Parse the allele number
                    allele_number = stoi(it->str());
                }



                // Make the name for its alt path
                string alt_path_name = "_alt_" + var_name + "_" + to_string(allele_number);

                for(auto& mapping : graph->paths.get_path(alt_path_name)) {
                    // Un-mark all nodes that are on this alt path, since it is used by the sample.
                    alt_path_ids.erase(mapping.position().node_id());
                }
            }

        };


        // Allocate a place to store actual variants
        vcflib::Variant var(variant_file);

        while (variant_file.is_open() && variant_file.getNextVariant(var)) {
            // this ... maybe we should remove it as for when we have calls against N
            bool isDNA = allATGC(var.ref);
            for (vector<string>::iterator a = var.alt.begin(); a != var.alt.end(); ++a) {
                if (!allATGC(*a)) isDNA = false;
            }
            // only work with DNA sequences
            if (!isDNA) {
                continue;
            }

            var.position -= 1; // convert to 0-based

            // Handle the variant
            handle_variant(var);
        }


        for(auto& node_id : alt_path_ids) {
            // And delete all the nodes that were used by alt paths that weren't
            // in the genotype of the first sample.

            for(auto& path_name : graph->paths.of_node(node_id)) {
                // For every path that touches the node we're destroying,
                // destroy the path. We can't leave it because it won't be the
                // same path without this node.
                graph->paths.remove_path(path_name);
#ifdef debug
                cerr << "Node " << node_id << " was on path " << path_name << endl;
#endif
            }

            // Actually get rid of the node once its paths are gone.
            graph->destroy_node(node_id);
        }

    }

    if (bluntify) {
        graph->bluntify();
    }

    if (!path_name.empty()) {
        graph->keep_path(path_name);
    }

    if (!paths_to_retain.empty() || retain_complement) {
        graph->paths.keep_paths(paths_to_retain);
    }

    if (drop_paths) {
        graph->paths.clear();
    }

    if (remove_orphans) {
        graph->remove_orphan_edges();
    }

    if (unchop) {
        graph->unchop();
    }

    if (simplify_graph) {
        graph->simplify_siblings();
    }

    if (normalize_graph) {
        graph->normalize();
    }

    if (until_normal_iter) {
        graph->normalize(until_normal_iter);
    }

    if (strong_connect) {
        graph->keep_multinode_strongly_connected_components();
    }

    if (remove_non_path) {
        graph->remove_non_path();
    }

    if (force_path_match) {
        graph->force_path_match();
    }

    if (orient_forward) {
        set<int64_t> flipped;
        graph->orient_nodes_forward(flipped);
    }

    if (flip_doubly_reversed_edges) {
        graph->flip_doubly_reversed_edges();
    }

    if (dagify_steps) {
        map<int64_t, pair<int64_t, bool> > node_translation;
        *graph = graph->dagify(dagify_steps, node_translation, 0, dagify_component_length_max);
    }

    if (dagify_to) {
        map<int64_t, pair<int64_t, bool> > node_translation;
        // use the walk as our maximum number of steps; it's the worst case
        *graph = graph->dagify(dagify_to, node_translation, dagify_to, dagify_component_length_max);
    }

    if (unfold_to) {
        map<int64_t, pair<int64_t, bool> > node_translation;
        *graph = graph->unfold(unfold_to, node_translation);
    }

    if (remove_null) {
        graph->remove_null_nodes_forwarding_edges();
    }

    if (sort_graph) {
        graph->sort();
    }

    if (break_cycles) {
        graph->break_cycles();
    }

    // to subset the graph
    if (!root_nodes.empty()) {
        VG g;
        for (auto root : root_nodes) {
            graph->nonoverlapping_node_context_without_paths(graph->get_node(root), g);
            graph->expand_context(g, max(context_steps, 1));
            g.remove_orphan_edges();
        }
        *graph = g;
    }

    if (!aln_file.empty()) {
        // read in the alignments and save their paths
        vector<Path> paths;
        function<void(Alignment&)> lambda = [&graph, &paths](Alignment& aln) {
            Path path = simplify(aln.path());
            path.set_name(aln.name());
            paths.push_back(path);
        };
        if (aln_file == "-") {
            stream::for_each(std::cin, lambda);
        } else {
            ifstream in;
            in.open(aln_file.c_str());
            stream::for_each(in, lambda);
        }
        if (!label_paths) {
            // execute the edits
            auto translation = graph->edit(paths);
            if (!translation_file.empty()) {
                ofstream out(translation_file);
                stream::write_buffered(out, translation, 0);
                out.close();
            }
        } else {
            // just add the path labels to the graph
            for (auto& path : paths) {
                graph->paths.extend(path);
            }
        }
    }

    if (!loci_file.empty()) {
        // read in the alignments and save their paths
        vector<Path> paths;
        function<void(Locus&)> lambda = [&graph, &paths, &called_genotypes_only](Locus& locus) {
            // if we are only doing called genotypes, record so we can filter alleles
            set<int> alleles_in_genotype;
            if (called_genotypes_only) {
                for (int i = 0; i < locus.genotype_size(); ++i) {
                    for (int j = 0; j < locus.genotype(i).allele_size(); ++j) {
                        alleles_in_genotype.insert(locus.genotype(i).allele(j));
                    }
                }
            }
            for (int i = 0; i < locus.allele_size(); ++i) {
                // skip alleles not in the genotype if using only called genotypes
                if (!alleles_in_genotype.empty()) {
                    if (!alleles_in_genotype.count(i)) continue;
                }
                Path path = simplify(locus.allele(i));
                stringstream name;
                name << locus.name() << ":" << i;
                path.set_name(name.str());
                paths.push_back(path);
            }
        };
        if (loci_file == "-") {
            stream::for_each(std::cin, lambda);
        } else {
            ifstream in;
            in.open(loci_file.c_str());
            stream::for_each(in, lambda);
        }
        // execute the edits and produce the translation if requested
        auto translation = graph->edit(paths);
        if (!translation_file.empty()) {
            ofstream out(translation_file);
            stream::write_buffered(out, translation, 0);
            out.close();
        }
    }

    // and optionally compact ids
    if (compact_ids) {
        graph->sort();
        graph->compact_ids();
    }

    if (compact_ranks) {
        graph->paths.compact_ranks();
    }

    if (prune_complex) {
        if (!(path_length > 0 && edge_max > 0)) {
            cerr << "[vg mod]: when pruning complex regions you must specify a --path-length and --edge-max" << endl;
            return 1;
        }
        graph->prune_complex_with_head_tail(path_length, edge_max);
    }

    if (prune_subgraphs) {
        graph->prune_short_subgraphs(path_length);
    }

    if (chop_to) {
        graph->dice_nodes(chop_to);
        graph->paths.compact_ranks();
    }

    if (kill_labels) {
        graph->for_each_node([](Node* n) { n->clear_sequence(); });
    }

    if (add_start_and_end_markers) {
        if (!(path_length > 0)) {
            cerr << "[vg mod]: when adding start and end markers you must provide a --path-length" << endl;
            return 1;
        }
        Node* head_node = NULL;
        Node* tail_node = NULL;
        graph->add_start_end_markers(path_length, '#', '$', head_node, tail_node);
    }

    if (destroy_node_id > 0) {
        graph->destroy_node(destroy_node_id);
    }

    if (cactus) {
        // ensure we're sorted
        graph->sort();
        *graph = cactusify(*graph);
        // no paths survive, make sure they are erased
        graph->paths = Paths();
    }

    graph->serialize_to_ostream(std::cout);

    delete graph;

    return 0;
}

// Register subcommand
static Subcommand vg_construct("mod", "filter, transform, and edit the graph", main_mod);
