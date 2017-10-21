#include "Odm25dMeshing.hpp"

int Odm25dMeshing::run(int argc, char **argv) {
	log << logFilePath << "\n";

	// If no arguments were passed, print help and return early.
	if (argc <= 1) {
		printHelp();
		return EXIT_SUCCESS;
	}

	try {

		parseArguments(argc, argv);

		loadPointCloud();

		detectPlanes();

		buildMesh();

	} catch (const Odm25dMeshingException& e) {
		log.setIsPrintingInCout(true);
		log << e.what() << "\n";
		log.printToFile(logFilePath);
		log << "For more detailed information, see log file." << "\n";
		return EXIT_FAILURE;
	} catch (const std::exception& e) {
		log.setIsPrintingInCout(true);
		log << "Error in OdmMeshing:\n";
		log << e.what() << "\n";
		log.printToFile(logFilePath);
		log << "For more detailed information, see log file." << "\n";
		return EXIT_FAILURE;
	}

	log.printToFile(logFilePath);

	return EXIT_SUCCESS;
}

void Odm25dMeshing::parseArguments(int argc, char **argv) {
	for (int argIndex = 1; argIndex < argc; ++argIndex) {
		// The argument to be parsed.
		std::string argument = std::string(argv[argIndex]);

		if (argument == "-help") {
			printHelp();
			exit(0);
		} else if (argument == "-verbose") {
			log.setIsPrintingInCout(true);
		} else if (argument == "-maxVertexCount" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc)
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> maxVertexCount;
			if (ss.bad())
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' has a bad value (wrong type).");
			maxVertexCount = std::max<unsigned int>(maxVertexCount, 0);
			log << "Vertex count was manually set to: " << maxVertexCount
					<< "\n";
		} else if (argument == "-wlopIterations" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc)
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> wlopIterations;
			if (ss.bad())
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' has a bad value (wrong type).");

			wlopIterations = std::min<unsigned int>(1000,
					std::max<unsigned int>(wlopIterations, 1));
			log << "WLOP iterations was manually set to: " << wlopIterations
					<< "\n";
		} else if (argument == "-inputFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			inputFile = std::string(argv[argIndex]);
			std::ifstream testFile(inputFile.c_str(), std::ios::binary);
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' has a bad value. (file not accessible)");
			}
			testFile.close();
			log << "Reading point cloud at: " << inputFile << "\n";
		} else if (argument == "-outputFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			outputFile = std::string(argv[argIndex]);
			std::ofstream testFile(outputFile.c_str());
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument + "' has a bad value.");
			}
			testFile.close();
			log << "Writing output to: " << outputFile << "\n";
		} else if (argument == "-logFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			logFilePath = std::string(argv[argIndex]);
			std::ofstream testFile(outputFile.c_str());
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument + "' has a bad value.");
			}
			testFile.close();
			log << "Writing log information to: " << logFilePath << "\n";
		} else {
			printHelp();
			throw Odm25dMeshingException(
					"Unrecognised argument '" + argument + "'");
		}
	}
}

void Odm25dMeshing::loadPointCloud() {
	const char CLASS_GROUND = 2;
	const float HAG_THRESHOLD = 1.0; // 1 meters

	pcl::PCLPointCloud2 blob;

	log << "Loading point cloud... ";

	if (pcl::io::loadPLYFile(inputFile.c_str(), blob) == -1) {
		throw Odm25dMeshingException("Error when reading from: " + inputFile);
	}

	log << "OK\n";

	log << "Scanning fields... ";

	pcl::PCLPointField *posX = NULL, *posY = NULL, *posZ = NULL,
			*normalX = NULL, *normalY = NULL, *normalZ = NULL, *classification = NULL,
			*hag = NULL;

#define ASSIGN(_name, _field) if (blob.fields[i].name == _name){ _field = &blob.fields[i]; log << _name << " "; continue; }

	for (size_t i = 0; i < blob.fields.size(); ++i) {
		ASSIGN("x", posX);
		ASSIGN("y", posY);
		ASSIGN("z", posZ);
		ASSIGN("normal_x", normalX);
		ASSIGN("normal_y", normalY);
		ASSIGN("normal_z", normalZ);
		ASSIGN("nx", normalX);
		ASSIGN("ny", normalY);
		ASSIGN("nz", normalZ);
		ASSIGN("classification", classification);
		ASSIGN("heightaboveground", hag);
	}

	log << "OK\n";

	if (posX == NULL || posY == NULL || posZ == NULL)
		throw Odm25dMeshingException(
				"Position attributes (x,y,z) missing from input");
	if (normalX == NULL || normalY == NULL || normalZ == NULL)
		throw Odm25dMeshingException(
				"Normal attributes (normal_x,normal_y,normal_z) missing from input");
	if (posX->datatype != pcl::PCLPointField::FLOAT32
			&& posX->datatype != pcl::PCLPointField::FLOAT64)
		throw Odm25dMeshingException(
				"Only float and float64 types are supported for position information");
	if (normalX->datatype != pcl::PCLPointField::FLOAT32
			&& normalX->datatype != pcl::PCLPointField::FLOAT64)
		throw Odm25dMeshingException(
				"Only float and float64 types are supported for normal information");

	if (classification->datatype != pcl::PCLPointField::UINT8) classification = NULL;
	if (classification == NULL) log << "WARNING: Classification attribute missing. Will treat all points as ground.\n";
	if (hag == NULL) log << "WARNING: heightaboveground attribute missing. Resulting mesh might have more artifacts.\n";

	uint8_t pointClass = 2;
	float pointHag = std::numeric_limits<float>::min();

	pcl::PointCloud<pcl::PointNormal> allPoints;

	allPoints.reserve(blob.width * blob.height);

	for (size_t point_step = 0, i = 0; point_step < blob.data.size();
			point_step += blob.point_step, i++) {
		uint8_t *point = blob.data.data() + point_step;

		if (posX->datatype == pcl::PCLPointField::FLOAT64) {
			allPoints[i].x =
					*(reinterpret_cast<double *>(point + posX->offset));
			allPoints[i].y =
					*(reinterpret_cast<double *>(point + posY->offset));
			allPoints[i].z =
					*(reinterpret_cast<double *>(point + posZ->offset));
		} else if (posX->datatype == pcl::PCLPointField::FLOAT32) {
			allPoints[i].x = *(reinterpret_cast<float *>(point + posX->offset));
			allPoints[i].y = *(reinterpret_cast<float *>(point + posY->offset));
			allPoints[i].z = *(reinterpret_cast<float *>(point + posZ->offset));
		}

		if (normalX->datatype == pcl::PCLPointField::FLOAT64) {
			allPoints[i].normal_x = *(reinterpret_cast<double *>(point
					+ normalX->offset));
			allPoints[i].normal_y = *(reinterpret_cast<double *>(point
					+ normalY->offset));
			allPoints[i].normal_z = *(reinterpret_cast<double *>(point
					+ normalZ->offset));
		} else if (normalX->datatype == pcl::PCLPointField::FLOAT32) {
			allPoints[i].normal_x = *(reinterpret_cast<float *>(point
					+ normalX->offset));
			allPoints[i].normal_y = *(reinterpret_cast<float *>(point
					+ normalY->offset));
			allPoints[i].normal_z = *(reinterpret_cast<float *>(point
					+ normalZ->offset));
		}

		if (classification != NULL) {
			pointClass = *(reinterpret_cast<uint8_t *>(point
					+ classification->offset));
		}

		if (hag != NULL) {
			if (hag->datatype == pcl::PCLPointField::FLOAT64)
				pointHag = *(reinterpret_cast<double *>(point + hag->offset));
			if (hag->datatype == pcl::PCLPointField::FLOAT32)
				pointHag = *(reinterpret_cast<float *>(point + hag->offset));
		}

		if (pointClass == CLASS_GROUND) {
			groundPoints->push_back(allPoints[i]);
		} else {
			if (pointHag >= HAG_THRESHOLD) {
				nongroundPoints->push_back(allPoints[i]);
			}else{
				groundPoints->push_back(allPoints[i]);
			}
		}
	}
	//	  flipFaces = interpreter.flip_faces();

	log << "Loaded " << groundPoints->size() << " ground points\n";
	log << "Loaded " << nongroundPoints->size() << " non-ground points\n";
}

// Detect planes from non-ground points
// and place them into the ground points (this just to avoid
// creating a new vector)
void Odm25dMeshing::detectPlanes() {
	const int K = 30,
		  MIN_CLUSTER_SIZE = 100;

	log << "Extracting clusters... ";

	pcl::search::Search<pcl::PointNormal>::Ptr tree = boost::shared_ptr<pcl::search::Search<pcl::PointNormal> > (new pcl::search::KdTree<pcl::PointNormal>);
	pcl::RegionGrowing<pcl::PointNormal, pcl::PointNormal> reg;
	reg.setMinClusterSize (MIN_CLUSTER_SIZE);
	reg.setMaxClusterSize (nongroundPoints->size());
	reg.setSearchMethod (tree);
	reg.setNumberOfNeighbours (K);
	reg.setInputCloud (nongroundPoints);
	reg.setInputNormals (nongroundPoints);

	reg.setSmoothnessThreshold (45 / 180.0 * M_PI);
	reg.setCurvatureThreshold (3);


	std::vector <pcl::PointIndices> clusters;
	reg.extract (clusters);

	pcl::io::savePLYFile("colored.ply", *reg.getColoredCloud ());

	log << " found " << clusters.size() << " clusters\n";
	log << "Computing per segment surface estimation...\n";

	std::vector<int> pointIdxNKNSearch(K);
	std::vector<float> pointNKNSquaredDistance(K);

	for (size_t cluster_idx = 0; cluster_idx < clusters.size(); cluster_idx++){
		pcl::PointCloud<pcl::PointNormal>::Ptr cluster_cloud(new pcl::PointCloud<pcl::PointNormal>(*nongroundPoints, clusters[cluster_idx].indices));
		pcl::KdTreeFLANN<pcl::PointNormal> kdtree;
		kdtree.setInputCloud(cluster_cloud);

		size_t surface_points = 0;
		size_t points_in_cluster = cluster_cloud.get()->points.size();

		for (auto point = cluster_cloud.get()->points.begin(); point != cluster_cloud.get()->points.end(); point++){
			size_t same_normal_direction_count = 0;

			// Find neighbors
			if (kdtree.nearestKSearch(*point, K, pointIdxNKNSearch, pointNKNSquaredDistance) > 0){
				size_t num_points = pointIdxNKNSearch.size();
				for (size_t k = 0; k < num_points; k++){
					const pcl::PointNormal &p = cluster_cloud.get()->at(pointIdxNKNSearch[k]);

					if (point->getNormalVector3fMap().dot(p.getNormalVector3fMap()) > 0.9){
						same_normal_direction_count++;
					}
				}
			}

			if (same_normal_direction_count >= pointIdxNKNSearch.size() * 0.8f) surface_points++;
		}

		float surface_ratio = (float)surface_points / (float)points_in_cluster;
		log << "Segment #" << cluster_idx << " (points: " << points_in_cluster << ", plane points: " << surface_points << " (" << (surface_ratio * 100.0f) << "%)\n";

		// At least 20% of points are surfaces in this segment
		// probably a man-made structure
		if (surface_ratio >= 0.2f && surface_points >= MIN_CLUSTER_SIZE){
			for (auto point_idx = clusters[cluster_idx].indices.begin (); point_idx != clusters[cluster_idx].indices.end (); point_idx++){
				groundPoints->push_back(nongroundPoints->points[*point_idx]);
			}
		}else{
			// Tree, or something else

			// Do nothing
		}
	}

	pcl::io::savePLYFile("filtered.ply", *groundPoints);

//	if (nongroundPoints->size() < 3) return;
//
//	pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
//	pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
//	// Create the segmentation object
//	pcl::SACSegmentationFromNormals<pcl::PointNormal, pcl::PointNormal> seg;
//	// Optional
//	seg.setOptimizeCoefficients (true);
//	// Mandatory
//	seg.setModelType (pcl::SACMODEL_NORMAL_PLANE);
//	seg.setMethodType (pcl::SAC_RANSAC);
//	seg.setDistanceThreshold (0.3);
//	seg.setNormalDistanceWeight(0.1);
//
//	seg.setInputCloud (nongroundPoints);
//	seg.setInputNormals(nongroundPoints);
//	seg.segment (*inliers, *coefficients);
//
//	pcl::PointCloud<pcl::PointNormal> filtered;
//	pcl::ExtractIndices<pcl::PointNormal> extract;
//	extract.setInputCloud (nongroundPoints);
//	extract.setIndices (inliers);
//	extract.setNegative (false);
//	extract.filter (filtered);
//
//	pcl::io::savePLYFile("nonground.ply", *nongroundPoints);
//	pcl::io::savePLYFile("filtered.ply", filtered);

	log << "Done!\n";
}

void Odm25dMeshing::buildMesh() {
//	size_t pointCount = groundPoints.size();
//
//	log << "Computing points average spacing... ";
//
//	FT avgSpacing = CGAL::compute_average_spacing<Concurrency_tag>(
//			groundPoints.begin(),
//			groundPoints.end(),
//			24);
//
//	log << avgSpacing << "\n";
//
//	log << "Grid Z sampling and smoothing... ";
//
//	size_t pointCountBeforeGridSampling = pointCount;
//
//	double gridStep = avgSpacing / 2.0;
//	Kernel::Iso_cuboid_3 bbox = CGAL::bounding_box(groundPoints.begin(), groundPoints.end());
//	Vector3 boxDiag = bbox.max() - bbox.min();
//
//	int gridWidth = 1 + static_cast<unsigned>(boxDiag.x() / gridStep + 0.5);
//	int gridHeight = 1 + static_cast<unsigned>(boxDiag.y() / gridStep + 0.5);
//
//	#define KEY(i, j) (i * gridWidth + j)
//
//	std::unordered_map<int, Point3> grid;
//
//	for (size_t c = 0; c < pointCount; c++){
//		const Point3 &p = groundPoints[c];
//		Vector3 relativePos = p - bbox.min();
//		int i = static_cast<int>((relativePos.x() / gridStep + 0.5));
//		int j = static_cast<int>((relativePos.y() / gridStep + 0.5));
//
//		if ((i >= 0 && i < gridWidth) && (j >= 0 && j < gridHeight)){
//			int key = KEY(i, j);
//
//			if (grid.find(key) == grid.end()){
//				grid[key] = p;
//			}else if ((!flipFaces && p.z() > grid[key].z()) || (flipFaces && p.z() < grid[key].z())){
//				grid[key] = p;
//			}
//		}
//	}
//
//	std::vector<FT> bucket;
//	unsigned int smoothedPoints = 0;
//
//	for (int i = 1; i < gridWidth - 1; i++){
//		for (int j = 1; j < gridHeight - 1; j++){
//			int key = KEY(i, j);
//
//			if (grid.find(key) != grid.end()){
//				const Point3 &p = grid[key];
//
//				for (int ni = i - 1; ni < i + 2; ni++){
//					for (int nj = j - 1; nj < j + 2; nj++){
//						if (ni == i && nj == j) continue;
//						int nkey = KEY(ni, nj);
//
//						if (grid.find(nkey) != grid.end()) bucket.push_back(grid[nkey].z());
//					}
//				}
//
//				if (bucket.size() >= 5){
//					FT mean = accumulate(bucket.begin(), bucket.end(), 0.0) / bucket.size();
//					FT variance = 0.0;
//
//					for (unsigned int k = 0; k < bucket.size(); k++) variance += fabs(bucket[k] - mean);
//					variance /= bucket.size();
//
//					if (fabs(p.z() - mean) >= 3 * variance){
//						// Replace Z value of outlier
//						grid[key] = Point3(p.x(), p.y(), mean);
//						smoothedPoints++;
//					}
//				}
//			}
//
//			bucket.clear();
//		}
//	}
//
//	std::vector<Point3> gridPoints;
//	for ( auto it = grid.begin(); it != grid.end(); ++it ){
//		gridPoints.push_back(it->second);
//	}
//
//	pointCount = gridPoints.size();
//	log << "smoothed " << smoothedPoints << " points, sampled " << (pointCountBeforeGridSampling - pointCount) << " points\n";
//
//	const double RETAIN_PERCENTAGE = std::min<double>(80., 100. * static_cast<double>(maxVertexCount) / static_cast<double>(pointCount));   // percentage of points to retain.
//	std::vector<Point3> simplifiedPoints;
//
//	log << "Performing weighted locally optimal projection simplification and regularization (retain: " << RETAIN_PERCENTAGE << "%, iterate: " << wlopIterations << ")" << "\n";
//
//	CGAL::wlop_simplify_and_regularize_point_set<Concurrency_tag>(
//			gridPoints.begin(),
//			gridPoints.end(),
//			std::back_inserter(simplifiedPoints),
//			RETAIN_PERCENTAGE,
//			-1,
//			wlopIterations,
//			true);
//
//	pointCount = simplifiedPoints.size();
//
//	if (pointCount < 3){
//		throw Odm25dMeshingException("Not enough points");
//	}
//
//	log << "Vertex count is " << pointCount << "\n";
//
//    log << "Jet smoothing... ";
//    for (size_t i = 1; i <= 5; i++){
//    	log << i << "...";
//    	CGAL::jet_smooth_point_set<Concurrency_tag>(simplifiedPoints.begin(), simplifiedPoints.end(), 24);
//    }
//    log << "OK\n";
//
//	typedef CDT::Point cgalPoint;
//	std::vector< std::pair<cgalPoint, size_t > > pts;
//	try{
//		pts.reserve(pointCount);
//	} catch (const std::bad_alloc&){
//		throw Odm25dMeshingException("Not enough memory");
//	}
//
//	for (size_t i = 0; i < pointCount; ++i){
//		pts.push_back(std::make_pair(cgalPoint(simplifiedPoints[i].x(), simplifiedPoints[i].y()), i));
//	}
//
//	log << "Computing delaunay triangulation... ";
//
//	CDT cdt;
//	cdt.insert(pts.begin(), pts.end());
//
//	unsigned int numberOfTriangles = static_cast<unsigned >(cdt.number_of_faces());
//	unsigned int triIndexes = numberOfTriangles*3;
//
//	if (numberOfTriangles == 0) throw Odm25dMeshingException("No triangles in resulting mesh");
//
//	log << numberOfTriangles << " triangles\n";
//
//	std::vector<float> vertices;
//	std::vector<int> vertexIndices;
//
//	try{
//		vertices.reserve(pointCount);
//		vertexIndices.reserve(triIndexes);
//	} catch (const std::bad_alloc&){
//		throw Odm25dMeshingException("Not enough memory");
//	}
//
//
//	log << "Saving mesh to file.\n";
//
//	std::filebuf fb;
//	fb.open(outputFile, std::ios::out);
//	std::ostream os(&fb);
//
//	os << "ply\n"
//	   << "format ascii 1.0\n"
//	   << "element vertex " << pointCount << "\n"
//	   << "property float x\n"
//	   << "property float y\n"
//	   << "property float z\n"
//	   << "element face " << numberOfTriangles << "\n"
//	   << "property list uchar int vertex_index\n"
//	   << "end_header\n";
//
//	for (size_t i = 0; i < pointCount; ++i){
//		os << simplifiedPoints[i].x() << " " << simplifiedPoints[i].y() << " " << simplifiedPoints[i].z() << std::endl;
//	}
//
//	for (CDT::Face_iterator face = cdt.faces_begin(); face != cdt.faces_end(); ++face) {
//		os << 3 << " ";
//
//		if (flipFaces){
//			os << face->vertex(2)->info() << " " << face->vertex(1)->info() << " " << face->vertex(0)->info() << std::endl;
//		}else{
//			os << face->vertex(0)->info() << " " << face->vertex(1)->info() << " " << face->vertex(2)->info() << std::endl;
//		}
//	}
//
//	fb.close();

	log << "Successfully wrote mesh to: " << outputFile << "\n";
}

void Odm25dMeshing::printHelp() {
	bool printInCoutPop = log.isPrintingInCout();
	log.setIsPrintingInCout(true);

	log << "Usage: odm_25dmeshing -inputFile [plyFile] [optional-parameters]\n";
	log
			<< "Create a 2.5D mesh from an oriented, classified point cloud (points with normals, classification and heightaboveground property) using a constrained delaunay triangulation. "
			<< "The program requires a path to an input PLY point cloud file, all other input parameters are optional.\n\n";

	log << "	-inputFile	<path>	to PLY point cloud\n"
			<< "	-outputFile	<path>	where the output PLY 2.5D mesh should be saved (default: "
			<< outputFile << ")\n"
			<< "	-logFile	<path>	log file path (default: " << logFilePath
			<< ")\n" << "	-verbose	whether to print verbose output (default: "
			<< (printInCoutPop ? "true" : "false") << ")\n"
			<< "	-maxVertexCount	<0 - N>	Maximum number of vertices in the output mesh. The mesh might have fewer vertices, but will not exceed this limit. (default: "
			<< maxVertexCount << ")\n"
			<< "	-wlopIterations	<1 - 1000>	Iterations of the Weighted Locally Optimal Projection (WLOP) simplification algorithm. Higher values take longer but produce a smoother mesh. (default: "
			<< wlopIterations << ")\n"

			<< "\n";

	log.setIsPrintingInCout(printInCoutPop);
}

