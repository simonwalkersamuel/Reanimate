#include "Network.hpp"
#include "spatGraph.hpp"

#include <string>
#include <dirent.h>

using namespace std;
using namespace reanimate;

void Network::setBuildPath(const string path, bool deleteFiles) {

    buildPath = path;

    printf("Setting build directory ...\n");
    DIR* dir = opendir(buildPath.c_str());
    if (dir) {
        if (deleteFiles) {
            printf("Directory exists, deleting contents\n");
            string contents = buildPath + "*";
            system(("rm " + contents).c_str());
        }
    }
    else {
        printf("Directory does not exist. Creating folder ...\n");
        system(("mkdir " + buildPath).c_str());
    }
    closedir(dir);

}

void Network::setLoadPath(const string path) {

    loadPath = path;

    printf("Setting build directory ...\n");
    DIR* dir = opendir(loadPath.c_str());
    if (!dir) {
        printf("Directory does not exist. Creating folder ...\n");
        system(("mkdir " + loadPath).c_str());
    }

    closedir(dir);

}

Network::Network() {

    gamma = 1./(1.e3*60); //  mm3/s / gamma -> nl/min
    alpha = 0.1333; // kg/mm.s2 / alpha -> mmHg (133.3 Pa)
    beta = 1.e-4;
    xi = 1e-6; // kg/mm.s / xi -> cP (1e-3 Pa.s)

    lthresh = 10.;

    // Constant visc
    constvisc = 3.;
    consthd = 0.45;
    nitmax = 100;

    kp = 0.1;
    ktau = 1.e-4;

    tissDensity = 1027 * 1e-9; // ~Density of tissue, kg / ml (connective tissue)
    bloodDensity = 1060 * 1e-9; // ~Density of blood, kg / ml

    phaseseparation = false;
    unknownBCs = false;
    silence = false;
    cuboidVessels = false;
    graphOverride = true;

    progressBar.set_todo_char(" ");
    progressBar.set_done_char("█");
    progressBar.set_opening_bracket_char("{");
    progressBar.set_closing_bracket_char("}");

    rLog = "Reanimate_Log.txt";

}
Network::~Network() = default;


void Network::loadNetwork(const string &filename, const bool cuboidVess, const bool directFromAmira)   {

    int max=200;
    char bb[200];

    if (!directFromAmira)   {networkPath = loadPath + filename;}
    else {networkPath = buildPath + filename;}
    initLog(); // Initialise log

    printf("Reading %s ...\n", networkPath.c_str());

    FILE *ifp;
    ifp = fopen(networkPath.c_str(),"r");

    fgets(bb,max,ifp);
    bb[strcspn(bb, "\n")] = 0;
    networkName = bb;

    printText(networkName, 3);
    printText("Importing network data");

    fscanf(ifp, "%lf %lf %lf\n", &alx,&aly,&alz); fgets(bb,max,ifp);
    fscanf(ifp, "%lli %lli %lli\n", &mxx,&myy,&mzz); fgets(bb,max,ifp);
    fscanf(ifp, "%lf\n", &lb); fgets(bb,max,ifp);
    fscanf(ifp, "%lf\n", &maxl); fgets(bb,max,ifp);
    fscanf(ifp,"%lli", &nodsegm);
    fgets(bb,max,ifp);

    printText("Tissue Dimensions (x,y,z) = " + to_string(int(alx)) + " um x " + to_string(int(aly)) + " um x " + to_string(int(alz)) + " um", 1, 0);

    // Number of segments in vessel network
    fscanf(ifp,"%i", &nseg); fgets(bb,max,ifp);
    fgets(bb,max,ifp);

    // Load segment data
    loadSegments(ifp, cuboidVess);

    // Number of nodes in vessel network
    fscanf(ifp,"%i", &nnod);
    fgets(bb,max,ifp);
    fgets(bb,max,ifp);

    // Coordinates of nodes
    nodname = zeros<ivec>(nnod);
    cnode = zeros<mat>(3,nnod);
    nodpress = zeros<vec>(nnod);

    int num = detect_col(ifp);
    if (num == 4)   {
        for(int inod = 0; inod < nnod; inod++)  {
            fscanf(ifp, "%lli %lf %lf %lf\n", &nodname(inod),&cnode(0,inod),&cnode(1,inod),&cnode(2,inod));
        }
    }
    else if (num == 5)  {
        for(int inod = 0; inod < nnod; inod++)  {
            fscanf(ifp, "%lli %lf %lf %lf %lf\n", &nodname(inod),&cnode(0,inod),&cnode(1,inod),&cnode(2,inod),&nodpress(inod));
        }
    }
    else    {
        printText("Network file -> Invalid Node Format "+ to_string(int(num)),4);
    }

    // Boundary nodes
    fscanf(ifp,"%i", &nnodbc);
    fgets(bb,max,ifp);
    fgets(bb,max,ifp);

    bcnodname = zeros<ivec>(nnodbc);
    bctyp = zeros<ivec>(nnodbc);
    bcprfl = zeros<vec>(nnodbc);
    bchd = zeros<vec>(nnodbc);

    nsol = detect_col(ifp);
    printText("nsol = " + to_string(int(nsol)) , 1, 0);
    printText("nnodbc = " + to_string(int(nnodbc)) , 1, 0);
    if (nsol == 4)   {
        for(int inodbc = 0; inodbc < nnodbc; inodbc++){
            fscanf(ifp,"%lli %lli %lf %lf\n", &bcnodname(inodbc),&bctyp(inodbc),&bcprfl(inodbc),&bchd(inodbc));
        }
        nsol -= 3;
        bcp = zeros<mat>(nnodbc,nsol);
    }
    else if (nsol > 4)  {
        nsol -= 4; // Count extra solutes
        bcp = zeros<mat>(nnodbc,nsol);
        for(int inodbc = 0; inodbc < nnodbc; inodbc++){
            fscanf(ifp,"%lli %lli %lf %lf", &bcnodname(inodbc),&bctyp(inodbc),&bcprfl(inodbc),&bchd(inodbc));
            for (int i = 0; i < nsol; i++)  {
                fscanf(ifp," %lf",&bcp(inodbc,i));
            }
            fscanf(ifp,"\n");
        }
    }
    else    {
        printText("Network file -> Invalid Boundary Node Format",4);
        if (nnodbc == 0)    {findBoundaryNodes();}
    }

    fclose(ifp);

    nodsegm += 1; // Armadillo indexing starts at zero

    setup_networkArrays();
    analyse_network();
    if (graphOverride) {
        npoint = nnod;
        edgeLabels = segname;
        ediam = diam;
        elseg = lseg;
        if (any(nodtyp == 2))   {printText("Spatial graph override initiated but node type 2 detected",5);}
    }
    else {edgeNetwork();}

    printNetwork("loadedNetworkFile.txt");

    if (num == 5)   {
        segpress = zeros<vec>(nseg);
        for (int iseg = 0; iseg < nseg; iseg++) {segpress(iseg) = (nodpress(ista(iseg)) + nodpress(iend(iseg)))/2.;}
        computeBoundaryFlow();
    }

    field<string> headers(2,1);
    headers(0,0) = "Diameter";
    headers(1,0) = "Length";
    mat data = zeros<mat>(nseg,2);
    data.col(0) = diam;
    data.col(1) = lseg;
    printHistogram("DiamLength_HistogramData.txt", data, headers);

}

void Network::loadSegments(FILE *ifp, const bool cuboidVess)    {

    // Segment properties: name type nodename(start), nodename(end), diameter, flow, hematocrit
    segname = zeros<ivec>(nseg);
    vesstyp = zeros<ivec>(nseg);
    segnodname = zeros<imat>(2,nseg);
    diam = zeros<vec>(nseg);
    lseg = zeros<vec>(nseg);
    q = zeros<vec>(nseg);
    hd = zeros<vec>(nseg);

    int num = detect_col(ifp);
    if (cuboidVess) {
        cuboidVessels = true;
        width = zeros<vec>(nseg);
        height = zeros<vec>(nseg);
        if (num == 8)   {
            for(int iseg = 0; iseg < nseg; iseg++){
                fscanf(ifp, "%lli %lli %lli %lli %lf %lf %lf %lf\n",
                       &segname(iseg),&vesstyp(iseg),&segnodname(0,iseg),&segnodname(1,iseg),&width(iseg),&height(iseg),&q(iseg),&hd(iseg));
                diam(iseg) = 0.5 * (width(iseg) + height(iseg));
            }
            computeLseg = 1;
        }
        else    {printText("Network File -> Invalid Segment Format",4);}
    }
    else {
        if (num == 7)   {
            for(int iseg = 0; iseg < nseg; iseg++){
                fscanf(ifp, "%lli %lli %lli %lli %lf %lf %lf\n",
                       &segname(iseg),&vesstyp(iseg),&segnodname(0,iseg),&segnodname(1,iseg),&diam(iseg),&q(iseg),&hd(iseg));
            }
            computeLseg = 1;
        }
        else if (num == 8)  {
            for(int iseg = 0; iseg < nseg; iseg++){
                fscanf(ifp, "%lli %lli %lli %lli %lf %lf %lf %lf\n",
                       &segname(iseg),&vesstyp(iseg),&segnodname(0,iseg),&segnodname(1,iseg),&diam(iseg),&lseg(iseg),&q(iseg),&hd(iseg));
            }
        }
        else    {printText("Network File -> Invalid Segment Format "+ to_string(int(num)),4);}
    }
    qq = abs(q);

}


void Network::subNetwork(ivec &index, bool graph, bool print) {

    if (print) {printText("Creating subnetwork");}

    uvec idx = find(index == 1);
    segname.shed_rows(idx);
    vesstyp.shed_rows(idx);
    segnodname.shed_cols(idx);
    diam.shed_rows(idx);
    lseg.shed_rows(idx);
    q.shed_rows(idx);
    hd.shed_rows(idx);
    if (edgeLabels.n_elem > 0) {
        edgeLabels.shed_rows(idx);
        elseg.shed_rows(idx);
        ediam.shed_rows(idx);
    }
    if (segpress.n_elem > 0)    {
        segpress.shed_rows(idx);
        qq.shed_rows(idx);
    }
    index.shed_rows(idx);
    nseg = segname.n_elem;

    // Populate connectivity indices
    ista = zeros<ivec>(nseg);
    iend = zeros<ivec>(nseg);
    indexSegmentConnectivity();

    // Update nodes types
    nodtyp.zeros();
    indexNodeConnectivity();
    idx = find(nodtyp == 0);
    nodname.shed_rows(idx);
    nodtyp.shed_rows(idx);
    cnode.shed_cols(idx);
    if (nodpress.n_elem > 0)    {nodpress.shed_rows(idx);}
    nnod = nodname.n_elem;

    // Update boundary nodes
    ivec copyBCnodname = bcnodname;
    ivec copyBCtyp = bctyp;
    vec copyBCprfl = bcprfl;
    vec copyBCHD = bchd;
    nnodbc = 0;
    for (int inod = 0; inod < nnod; inod++) {
        if (nodtyp(inod) == 1)  {
            nnodbc += 1;
        }
    }

    bcnodname = zeros<ivec>(nnodbc);
    bctyp = zeros<ivec>(nnodbc);
    bcprfl = zeros<vec>(nnodbc);
    bchd = zeros<vec>(nnodbc);
    BCgeo = zeros<ivec>(nnodbc);

    int jnodbc = 0;
    int found{};
    for (int inod = 0; inod < nnod; inod++) {
        if (nodtyp(inod) == 1)  {
            bcnodname(jnodbc) = nodname(inod);
            for (int knodbc = 0; knodbc < (int) copyBCnodname.n_elem; knodbc++)   {
                if (bcnodname(jnodbc) == copyBCnodname(knodbc)) {
                    bctyp(jnodbc) = copyBCtyp(knodbc);
                    bcprfl(jnodbc) = copyBCprfl(knodbc);
                    bchd(jnodbc) = copyBCHD(knodbc);
                    knodbc = copyBCnodname.n_elem;
                    found = 1;
                }
            }
            if (found == 0) {
                if (nodpress(inod) > 0.)    {
                    bctyp(jnodbc) = 0;
                    bcprfl(jnodbc) = nodpress(inod);
                }
                else {bctyp(jnodbc) = -3;}
            }
            jnodbc += 1;
            found = 0;
        }
    }

    setup_networkArrays();
    analyse_network(graph,print);

}

void Network::removeNewBC(ivec storeBCnodname, bool print, bool graph) {

    ivec newBC = zeros<ivec>(nnod);
    for (int inodbc = 0; inodbc < nnodbc; inodbc++) {
        uvec idx = find(bcnodname(inodbc) == storeBCnodname);
        if (idx.n_elem == 0)    {newBC(bcnod(inodbc)) = 1;}
    }

    ivec removeBridge = zeros<ivec>(nnod);
    for (int iseg = 0; iseg < nseg; iseg++) {
        ivec tagNode = -ones<ivec>(nnod);
        if (newBC(ista(iseg)) == 1)    {
            dfsBasic(iend(iseg),1,tagNode, true, 2);
            tagNode(ista(iseg)) = 1;
        }
        else if (newBC(iend(iseg)) == 1)   {
            dfsBasic(ista(iseg),1,tagNode, true, 2);
            tagNode(iend(iseg)) = 1;
        }
        uvec idx  = find(tagNode == 1);
        if (idx.n_elem > 0) {removeBridge(idx).fill(1);}
    }
    ivec removeSegments = zeros<ivec>(nseg);
    for (int iseg = 0; iseg < nseg; iseg++) {
        if (removeBridge(ista(iseg)) == 1 && removeBridge(iend(iseg)) == 1) {removeSegments(iseg) = 1;}
    }
    subNetwork(removeSegments, graph, print);

}

void Network::findBoundaryNodes()    {

    setup_networkArrays();
    indexSegmentConnectivity();

    int inod1{},inod2{};
    for (int iseg = 0; iseg < nseg; iseg++) {
        inod1 = (int) ista(iseg);
        inod2 = (int) iend(iseg);
        nodtyp(inod1) += 1;
        nodtyp(inod2) += 1;
    }
    uvec idx = find(nodtyp == 1);
    nnodbc = (int) idx.n_elem;

    bcnodname = nodname(idx);
    bctyp = 3 * ones<ivec>(nnodbc);
    bcprfl = zeros<vec>(nnodbc);
    bchd = consthd * ones<vec>(nnodbc);
    BCgeo = zeros<ivec>(nnodbc);

}