#pragma once

#include <algorithm>      // min, max
#include <cassert>        // assert
#include <ostream>        // ostream
#include <string>         // string
#include <unordered_map>  // unordered_map
#include <vector>         // vector

namespace abby2 {
const unsigned int NULL_NODE = 0xffffffff;

/*! \brief The axis-aligned bounding box object.

    Axis-aligned bounding boxes (AABBs) store information for the minimum
    orthorhombic bounding-box for an object. Support is provided for
    dimensions >= 2. (In 2D the bounding box is either a rectangle,
    in 3D it is a rectangular prism.)

    Class member functions provide functionality for merging AABB objects
    and testing overlap with other AABBs.
 */
class AABB
{
 public:
  AABB() = default;

  explicit AABB(unsigned int dimension)
  {
    assert(dimension >= 2);

    lowerBound.resize(dimension);
    upperBound.resize(dimension);
  }

  AABB(const std::vector<double>& lowerBound_,
       const std::vector<double>& upperBound_)
  {
    // Validate the dimensionality of the bounds vectors.
    if (lowerBound.size() != upperBound.size()) {
      throw std::invalid_argument("[ERROR]: Dimensionality mismatch!");
    }

    // Validate that the upper bounds exceed the lower bounds.
    for (unsigned int i = 0; i < lowerBound.size(); i++) {
      // Validate the bound.
      if (lowerBound[i] > upperBound[i]) {
        throw std::invalid_argument(
            "[ERROR]: AABB lower bound is greater than the upper bound!");
      }
    }

    surfaceArea = computeSurfaceArea();
    centre = computeCentre();
  }

  double computeSurfaceArea() const
  {
    // Sum of "area" of all the sides.
    double sum = 0;

    // General formula for one side: hold one dimension constant
    // and multiply by all the other ones.
    for (unsigned int d1 = 0; d1 < lowerBound.size(); d1++) {
      // "Area" of current side.
      double product = 1;

      for (unsigned int d2 = 0; d2 < lowerBound.size(); d2++) {
        if (d1 == d2) continue;

        double dx = upperBound[d2] - lowerBound[d2];
        product *= dx;
      }

      // Update the sum.
      sum += product;
    }

    return 2.0 * sum;
  }

  double getSurfaceArea() const
  {
    return surfaceArea;
  }

  void merge(const AABB& aabb1, const AABB& aabb2)
  {
    assert(aabb1.lowerBound.size() == aabb2.lowerBound.size());
    assert(aabb1.upperBound.size() == aabb2.upperBound.size());

    lowerBound.resize(aabb1.lowerBound.size());
    upperBound.resize(aabb1.lowerBound.size());

    for (unsigned int i = 0; i < lowerBound.size(); i++) {
      lowerBound[i] = std::min(aabb1.lowerBound[i], aabb2.lowerBound[i]);
      upperBound[i] = std::max(aabb1.upperBound[i], aabb2.upperBound[i]);
    }

    surfaceArea = computeSurfaceArea();
    centre = computeCentre();
  }

  bool contains(const AABB& aabb) const
  {
    assert(aabb.lowerBound.size() == lowerBound.size());

    for (unsigned int i = 0; i < lowerBound.size(); i++) {
      if (aabb.lowerBound[i] < lowerBound[i]) return false;
      if (aabb.upperBound[i] > upperBound[i]) return false;
    }

    return true;
  }

  bool overlaps(const AABB& aabb, bool touchIsOverlap) const
  {
    assert(aabb.lowerBound.size() == lowerBound.size());

    bool rv = true;

    if (touchIsOverlap) {
      for (unsigned int i = 0; i < lowerBound.size(); ++i) {
        if (aabb.upperBound[i] < lowerBound[i] ||
            aabb.lowerBound[i] > upperBound[i]) {
          rv = false;
          break;
        }
      }
    } else {
      for (unsigned int i = 0; i < lowerBound.size(); ++i) {
        if (aabb.upperBound[i] <= lowerBound[i] ||
            aabb.lowerBound[i] >= upperBound[i]) {
          rv = false;
          break;
        }
      }
    }

    return rv;
  }

  std::vector<double> computeCentre()
  {
    std::vector<double> position(lowerBound.size());

    for (unsigned int i = 0; i < position.size(); i++)
      position[i] = 0.5 * (lowerBound[i] + upperBound[i]);

    return position;
  }

  void setDimension(unsigned int dimension)
  {
    assert(dimension >= 2);

    lowerBound.resize(dimension);
    upperBound.resize(dimension);
  }

  std::vector<double> lowerBound;
  std::vector<double> upperBound;
  std::vector<double> centre;
  double surfaceArea;
};

/*! \brief A node of the AABB tree.

    Each node of the tree contains an AABB object which corresponds to a
    particle, or a group of particles, in the simulation box. The AABB
    objects of individual particles are "fattened" before they are stored
    to avoid having to continually update and rebalance the tree when
    displacements are small.

    Nodes are aware of their position within in the tree. The isLeaf member
    function allows the tree to query whether the node is a leaf, i.e. to
    determine whether it holds a single particle.
 */
struct Node
{
  AABB aabb;
  unsigned int parent;
  unsigned int next;
  unsigned int left;
  unsigned int right;
  int height;
  unsigned int particle;

  /*! \return
          Whether the node is a leaf node.
   */
  bool isLeaf() const
  {
    return (left == NULL_NODE);
  }
};

/*! \brief The dynamic AABB tree.

    The dynamic AABB tree is a hierarchical data structure that can be used
    to efficiently query overlaps between objects of arbitrary shape and
    size that lie inside of a simulation box. Support is provided for
    periodic and non-periodic boxes, as well as boxes with partial
    periodicity, e.g. periodic along specific axes.
 */
class Tree
{
 public:
  explicit Tree(unsigned int dimension_ = 3,
                double skinThickness_ = 0.05,
                unsigned int nParticles = 16,
                bool touchIsOverlap = true)
      : dimension(dimension_),
        skinThickness(skinThickness_),
        touchIsOverlap(touchIsOverlap)
  {
    // Validate the dimensionality.
    if ((dimension < 2)) {
      throw std::invalid_argument("[ERROR]: Invalid dimensionality!");
    }

    // Initialise the tree.
    root = NULL_NODE;
    nodeCount = 0;
    nodeCapacity = nParticles;
    nodes.resize(nodeCapacity);

    // Build a linked list for the list of free nodes.
    for (unsigned int i = 0; i < nodeCapacity - 1; i++) {
      nodes[i].next = i + 1;
      nodes[i].height = -1;
    }
    nodes[nodeCapacity - 1].next = NULL_NODE;
    nodes[nodeCapacity - 1].height = -1;

    // Assign the index of the first free node.
    freeList = 0;
  }

  void insertParticle(unsigned int particle,
                      std::vector<double>& lowerBound,
                      std::vector<double>& upperBound)
  {
    // Make sure the particle doesn't already exist.
    if (particleMap.count(particle) != 0) {
      throw std::invalid_argument("[ERROR]: Particle already exists in tree!");
    }

    // Validate the dimensionality of the bounds vectors.
    if ((lowerBound.size() != dimension) || (upperBound.size() != dimension)) {
      throw std::invalid_argument("[ERROR]: Dimensionality mismatch!");
    }

    // Allocate a new node for the particle.
    unsigned int node = allocateNode();

    // AABB size in each dimension.
    std::vector<double> size(dimension);

    // Compute the AABB limits.
    for (unsigned int i = 0; i < dimension; i++) {
      // Validate the bound.
      if (lowerBound[i] > upperBound[i]) {
        throw std::invalid_argument(
            "[ERROR]: AABB lower bound is greater than the upper bound!");
      }

      nodes[node].aabb.lowerBound[i] = lowerBound[i];
      nodes[node].aabb.upperBound[i] = upperBound[i];
      size[i] = upperBound[i] - lowerBound[i];
    }

    // Fatten the AABB.
    for (unsigned int i = 0; i < dimension; i++) {
      nodes[node].aabb.lowerBound[i] -= skinThickness * size[i];
      nodes[node].aabb.upperBound[i] += skinThickness * size[i];
    }
    nodes[node].aabb.surfaceArea = nodes[node].aabb.computeSurfaceArea();
    nodes[node].aabb.centre = nodes[node].aabb.computeCentre();

    // Zero the height.
    nodes[node].height = 0;

    // Insert a new leaf into the tree.
    insertLeaf(node);

    // Add the new particle to the map.
    particleMap.insert(
        std::unordered_map<unsigned int, unsigned int>::value_type(particle,
                                                                   node));

    // Store the particle index.
    nodes[node].particle = particle;
  }

  unsigned int nParticles()
  {
    return particleMap.size();
  }

  void removeParticle(unsigned int particle)
  {
    // Map iterator.
    std::unordered_map<unsigned int, unsigned int>::iterator it;

    // Find the particle.
    it = particleMap.find(particle);

    // The particle doesn't exist.
    if (it == particleMap.end()) {
      throw std::invalid_argument("[ERROR]: Invalid particle index!");
    }

    // Extract the node index.
    unsigned int node = it->second;

    // Erase the particle from the map.
    particleMap.erase(it);

    assert(node < nodeCapacity);
    assert(nodes[node].isLeaf());

    removeLeaf(node);
    freeNode(node);
  }

  void removeAll()
  {
    // Iterator pointing to the start of the particle map.
    std::unordered_map<unsigned int, unsigned int>::iterator it =
        particleMap.begin();

    // Iterate over the map.
    while (it != particleMap.end()) {
      // Extract the node index.
      unsigned int node = it->second;

      assert(node < nodeCapacity);
      assert(nodes[node].isLeaf());

      removeLeaf(node);
      freeNode(node);

      it++;
    }

    // Clear the particle map.
    particleMap.clear();
  }

  void print(std::ostream& stream) const
  {
    stream << "aabbcc:\n";
    print(stream, "", root, false);
  }

  void print(std::ostream& stream,
             const std::string& prefix,
             unsigned int index,
             bool isLeft) const
  {
    if (index != NULL_NODE) {
      const auto& node = nodes.at(index);

      stream << prefix << (isLeft ? "├── " : "└── ");
      if (node.isLeaf()) {
        stream << node.particle << "\n";
      } else {
        stream << "X\n";
      }

      print(stream, prefix + (isLeft ? "│   " : "    "), node.left, true);
      print(stream, prefix + (isLeft ? "│   " : "    "), node.right, false);
    }
  }

  bool updateParticle(unsigned int particle,
                      std::vector<double>& lowerBound,
                      std::vector<double>& upperBound,
                      bool alwaysReinsert = false)
  {
    // Validate the dimensionality of the bounds vectors.
    if ((lowerBound.size() != dimension) && (upperBound.size() != dimension)) {
      throw std::invalid_argument("[ERROR]: Dimensionality mismatch!");
    }

    // Map iterator.
    std::unordered_map<unsigned int, unsigned int>::iterator it;

    // Find the particle.
    it = particleMap.find(particle);

    // The particle doesn't exist.
    if (it == particleMap.end()) {
      throw std::invalid_argument("[ERROR]: Invalid particle index!");
    }

    // Extract the node index.
    unsigned int node = it->second;

    assert(node < nodeCapacity);
    assert(nodes[node].isLeaf());

    // AABB size in each dimension.
    std::vector<double> size(dimension);

    // Compute the AABB limits.
    for (unsigned int i = 0; i < dimension; i++) {
      // Validate the bound.
      if (lowerBound[i] > upperBound[i]) {
        throw std::invalid_argument(
            "[ERROR]: AABB lower bound is greater than the upper bound!");
      }

      size[i] = upperBound[i] - lowerBound[i];
    }

    // Create the new AABB.
    AABB aabb(lowerBound, upperBound);

    // No need to update if the particle is still within its fattened AABB.
    if (!alwaysReinsert && nodes[node].aabb.contains(aabb)) return false;

    // Remove the current leaf.
    removeLeaf(node);

    // Fatten the new AABB.
    for (unsigned int i = 0; i < dimension; i++) {
      aabb.lowerBound[i] -= skinThickness * size[i];
      aabb.upperBound[i] += skinThickness * size[i];
    }

    // Assign the new AABB.
    nodes[node].aabb = aabb;

    // Update the surface area and centroid.
    nodes[node].aabb.surfaceArea = nodes[node].aabb.computeSurfaceArea();
    nodes[node].aabb.centre = nodes[node].aabb.computeCentre();

    // Insert a new leaf node.
    insertLeaf(node);

    return true;
  }

  //! Query the tree to find candidate interactions for a particle.
  /*! \param particle
          The particle index.

      \return particles
          A vector of particle indices.
   */
  std::vector<unsigned int> query(unsigned int particle)
  {
    // Make sure that this is a valid particle.
    if (particleMap.count(particle) == 0) {
      throw std::invalid_argument("[ERROR]: Invalid particle index!");
    }

    // Test overlap of particle AABB against all other particles.
    return query(particle, nodes[particleMap.find(particle)->second].aabb);
  }

  std::vector<unsigned int> query(unsigned int particle, const AABB& aabb)
  {
    std::vector<unsigned int> stack;
    stack.reserve(256);
    stack.push_back(root);

    std::vector<unsigned int> particles;

    while (stack.size() > 0) {
      unsigned int node = stack.back();
      stack.pop_back();

      // Copy the AABB.
      AABB nodeAABB = nodes[node].aabb;

      if (node == NULL_NODE) continue;

      // Test for overlap between the AABBs.
      if (aabb.overlaps(nodeAABB, touchIsOverlap)) {
        // Check that we're at a leaf node.
        if (nodes[node].isLeaf()) {
          // Can't interact with itself.
          if (nodes[node].particle != particle) {
            particles.push_back(nodes[node].particle);
          }
        } else {
          stack.push_back(nodes[node].left);
          stack.push_back(nodes[node].right);
        }
      }
    }

    return particles;
  }

  //! Query the tree to find candidate interactions for an AABB.
  /*! \param aabb
          The AABB.

      \return particles
          A vector of particle indices.
   */
  std::vector<unsigned int> query(const AABB& aabb)
  {
    // Make sure the tree isn't empty.
    if (particleMap.size() == 0) {
      return std::vector<unsigned int>();
    }

    // Test overlap of AABB against all particles.
    return query(std::numeric_limits<unsigned int>::max(), aabb);
  }

  //! Get a particle AABB.
  /*! \param particle
          The particle index.
   */
  const AABB& getAABB(unsigned int particle)
  {
    return nodes[particleMap[particle]].aabb;
  }

  //! Get the height of the tree.
  /*! \return
          The height of the binary tree.
   */
  unsigned int getHeight() const
  {
    if (root == NULL_NODE) return 0;
    return nodes[root].height;
  }

  //! Get the number of nodes in the tree.
  /*! \return
          The number of nodes in the tree.
   */
  unsigned int getNodeCount() const
  {
    return nodeCount;
  }

  //! Compute the maximum balancance of the tree.
  /*! \return
          The maximum difference between the height of two
          children of a node.
   */
  unsigned int computeMaximumBalance() const
  {
    unsigned int maxBalance = 0;
    for (unsigned int i = 0; i < nodeCapacity; i++) {
      if (nodes[i].height <= 1) continue;

      assert(nodes[i].isLeaf() == false);

      unsigned int balance =
          std::abs(nodes[nodes[i].left].height - nodes[nodes[i].right].height);
      maxBalance = std::max(maxBalance, balance);
    }

    return maxBalance;
  }

  //! Compute the surface area ratio of the tree.
  /*! \return
          The ratio of the sum of the node surface area to the surface
          area of the root node.
   */
  double computeSurfaceAreaRatio() const
  {
    if (root == NULL_NODE) return 0.0;

    double rootArea = nodes[root].aabb.computeSurfaceArea();
    double totalArea = 0.0;

    for (unsigned int i = 0; i < nodeCapacity; i++) {
      if (nodes[i].height < 0) continue;

      totalArea += nodes[i].aabb.computeSurfaceArea();
    }

    return totalArea / rootArea;
  }

  /// Validate the tree.
  void validate() const
  {
#ifndef NDEBUG
    validateStructure(root);
    validateMetrics(root);

    unsigned int freeCount = 0;
    unsigned int freeIndex = freeList;

    while (freeIndex != NULL_NODE) {
      assert(freeIndex < nodeCapacity);
      freeIndex = nodes[freeIndex].next;
      freeCount++;
    }

    assert(getHeight() == computeHeight());
    assert((nodeCount + freeCount) == nodeCapacity);
#endif
  }

  /// Rebuild an optimal tree.
  void rebuild()
  {
    std::vector<unsigned int> nodeIndices(nodeCount);
    unsigned int count = 0;

    for (unsigned int i = 0; i < nodeCapacity; i++) {
      // Free node.
      if (nodes[i].height < 0) continue;

      if (nodes[i].isLeaf()) {
        nodes[i].parent = NULL_NODE;
        nodeIndices[count] = i;
        count++;
      } else
        freeNode(i);
    }

    while (count > 1) {
      double minCost = std::numeric_limits<double>::max();
      int iMin = -1, jMin = -1;

      for (unsigned int i = 0; i < count; i++) {
        AABB aabbi = nodes[nodeIndices[i]].aabb;

        for (unsigned int j = i + 1; j < count; j++) {
          AABB aabbj = nodes[nodeIndices[j]].aabb;
          AABB aabb;
          aabb.merge(aabbi, aabbj);
          double cost = aabb.getSurfaceArea();

          if (cost < minCost) {
            iMin = i;
            jMin = j;
            minCost = cost;
          }
        }
      }

      unsigned int index1 = nodeIndices[iMin];
      unsigned int index2 = nodeIndices[jMin];

      unsigned int parent = allocateNode();
      nodes[parent].left = index1;
      nodes[parent].right = index2;
      nodes[parent].height =
          1 + std::max(nodes[index1].height, nodes[index2].height);
      nodes[parent].aabb.merge(nodes[index1].aabb, nodes[index2].aabb);
      nodes[parent].parent = NULL_NODE;

      nodes[index1].parent = parent;
      nodes[index2].parent = parent;

      nodeIndices[jMin] = nodeIndices[count - 1];
      nodeIndices[iMin] = parent;
      count--;
    }

    root = nodeIndices[0];

    validate();
  }

 private:
  /// The index of the root node.
  unsigned int root;

  /// The dynamic tree.
  std::vector<Node> nodes;

  /// The current number of nodes in the tree.
  unsigned int nodeCount;

  /// The current node capacity.
  unsigned int nodeCapacity;

  /// The position of node at the top of the free list.
  unsigned int freeList;

  /// The dimensionality of the system.
  unsigned int dimension;

  /// The skin thickness of the fattened AABBs, as a fraction of the AABB base
  /// length.
  double skinThickness;

  /// A map between particle and node indices.
  std::unordered_map<unsigned int, unsigned int> particleMap;

  /// Does touching count as overlapping in tree queries?
  bool touchIsOverlap;

  //! Allocate a new node.
  /*! \return
          The index of the allocated node.
   */
  unsigned int allocateNode()
  {
    // Exand the node pool as needed.
    if (freeList == NULL_NODE) {
      assert(nodeCount == nodeCapacity);

      // The free list is empty. Rebuild a bigger pool.
      nodeCapacity *= 2;
      nodes.resize(nodeCapacity);

      // Build a linked list for the list of free nodes.
      for (unsigned int i = nodeCount; i < nodeCapacity - 1; i++) {
        nodes[i].next = i + 1;
        nodes[i].height = -1;
      }
      nodes[nodeCapacity - 1].next = NULL_NODE;
      nodes[nodeCapacity - 1].height = -1;

      // Assign the index of the first free node.
      freeList = nodeCount;
    }

    // Peel a node off the free list.
    unsigned int node = freeList;
    freeList = nodes[node].next;
    nodes[node].parent = NULL_NODE;
    nodes[node].left = NULL_NODE;
    nodes[node].right = NULL_NODE;
    nodes[node].height = 0;
    nodes[node].aabb.setDimension(dimension);
    nodeCount++;

    return node;
  }

  //! Free an existing node.
  /*! \param node
          The index of the node to be freed.
   */
  void freeNode(unsigned int node)
  {
    assert(node < nodeCapacity);
    assert(0 < nodeCount);

    nodes[node].next = freeList;
    nodes[node].height = -1;
    freeList = node;
    nodeCount--;
  }

  //! Insert a leaf into the tree.
  /*! \param leaf
          The index of the leaf node.
   */
  void insertLeaf(unsigned int leaf)
  {
    if (root == NULL_NODE) {
      root = leaf;
      nodes[root].parent = NULL_NODE;
      return;
    }

    // Find the best sibling for the node.

    const AABB leafAABB = nodes[leaf].aabb;
    unsigned int index = root;

    while (!nodes[index].isLeaf()) {
      // Extract the children of the node.
      unsigned int left = nodes[index].left;
      unsigned int right = nodes[index].right;

      double surfaceArea = nodes[index].aabb.getSurfaceArea();

      AABB combinedAABB;
      combinedAABB.merge(nodes[index].aabb, leafAABB);
      double combinedSurfaceArea = combinedAABB.getSurfaceArea();

      // Cost of creating a new parent for this node and the new leaf.
      double cost = 2.0 * combinedSurfaceArea;

      // Minimum cost of pushing the leaf further down the tree.
      double inheritanceCost = 2.0 * (combinedSurfaceArea - surfaceArea);

      // Cost of descending to the left.
      double costLeft;
      if (nodes[left].isLeaf()) {
        AABB aabb;
        aabb.merge(leafAABB, nodes[left].aabb);
        costLeft = aabb.getSurfaceArea() + inheritanceCost;
      } else {
        AABB aabb;
        aabb.merge(leafAABB, nodes[left].aabb);
        double oldArea = nodes[left].aabb.getSurfaceArea();
        double newArea = aabb.getSurfaceArea();
        costLeft = (newArea - oldArea) + inheritanceCost;
      }

      // Cost of descending to the right.
      double costRight;
      if (nodes[right].isLeaf()) {
        AABB aabb;
        aabb.merge(leafAABB, nodes[right].aabb);
        costRight = aabb.getSurfaceArea() + inheritanceCost;
      } else {
        AABB aabb;
        aabb.merge(leafAABB, nodes[right].aabb);
        double oldArea = nodes[right].aabb.getSurfaceArea();
        double newArea = aabb.getSurfaceArea();
        costRight = (newArea - oldArea) + inheritanceCost;
      }

      // Descend according to the minimum cost.
      if ((cost < costLeft) && (cost < costRight)) break;

      // Descend.
      if (costLeft < costRight)
        index = left;
      else
        index = right;
    }

    unsigned int sibling = index;

    // Create a new parent.
    unsigned int oldParent = nodes[sibling].parent;
    unsigned int newParent = allocateNode();
    nodes[newParent].parent = oldParent;
    nodes[newParent].aabb.merge(leafAABB, nodes[sibling].aabb);
    nodes[newParent].height = nodes[sibling].height + 1;

    // The sibling was not the root.
    if (oldParent != NULL_NODE) {
      if (nodes[oldParent].left == sibling)
        nodes[oldParent].left = newParent;
      else
        nodes[oldParent].right = newParent;

      nodes[newParent].left = sibling;
      nodes[newParent].right = leaf;
      nodes[sibling].parent = newParent;
      nodes[leaf].parent = newParent;
    }
    // The sibling was the root.
    else {
      nodes[newParent].left = sibling;
      nodes[newParent].right = leaf;
      nodes[sibling].parent = newParent;
      nodes[leaf].parent = newParent;
      root = newParent;
    }

    // Walk back up the tree fixing heights and AABBs.
    index = nodes[leaf].parent;
    while (index != NULL_NODE) {
      index = balance(index);

      unsigned int left = nodes[index].left;
      unsigned int right = nodes[index].right;

      assert(left != NULL_NODE);
      assert(right != NULL_NODE);

      nodes[index].height =
          1 + std::max(nodes[left].height, nodes[right].height);
      nodes[index].aabb.merge(nodes[left].aabb, nodes[right].aabb);

      index = nodes[index].parent;
    }
  }

  //! Remove a leaf from the tree.
  /*! \param leaf
          The index of the leaf node.
   */
  void removeLeaf(unsigned int leaf)
  {
    if (leaf == root) {
      root = NULL_NODE;
      return;
    }

    unsigned int parent = nodes[leaf].parent;
    unsigned int grandParent = nodes[parent].parent;
    unsigned int sibling;

    if (nodes[parent].left == leaf)
      sibling = nodes[parent].right;
    else
      sibling = nodes[parent].left;

    // Destroy the parent and connect the sibling to the grandparent.
    if (grandParent != NULL_NODE) {
      if (nodes[grandParent].left == parent)
        nodes[grandParent].left = sibling;
      else
        nodes[grandParent].right = sibling;

      nodes[sibling].parent = grandParent;
      freeNode(parent);

      // Adjust ancestor bounds.
      unsigned int index = grandParent;
      while (index != NULL_NODE) {
        index = balance(index);

        unsigned int left = nodes[index].left;
        unsigned int right = nodes[index].right;

        nodes[index].aabb.merge(nodes[left].aabb, nodes[right].aabb);
        nodes[index].height =
            1 + std::max(nodes[left].height, nodes[right].height);

        index = nodes[index].parent;
      }
    } else {
      root = sibling;
      nodes[sibling].parent = NULL_NODE;
      freeNode(parent);
    }
  }

  //! Balance the tree.
  /*! \param node
          The index of the node.
   */
  unsigned int balance(unsigned int node)
  {
    assert(node != NULL_NODE);

    if (nodes[node].isLeaf() || (nodes[node].height < 2)) return node;

    unsigned int left = nodes[node].left;
    unsigned int right = nodes[node].right;

    assert(left < nodeCapacity);
    assert(right < nodeCapacity);

    int currentBalance = nodes[right].height - nodes[left].height;

    // Rotate right branch up.
    if (currentBalance > 1) {
      unsigned int rightLeft = nodes[right].left;
      unsigned int rightRight = nodes[right].right;

      assert(rightLeft < nodeCapacity);
      assert(rightRight < nodeCapacity);

      // Swap node and its right-hand child.
      nodes[right].left = node;
      nodes[right].parent = nodes[node].parent;
      nodes[node].parent = right;

      // The node's old parent should now point to its right-hand child.
      if (nodes[right].parent != NULL_NODE) {
        if (nodes[nodes[right].parent].left == node)
          nodes[nodes[right].parent].left = right;
        else {
          assert(nodes[nodes[right].parent].right == node);
          nodes[nodes[right].parent].right = right;
        }
      } else
        root = right;

      // Rotate.
      if (nodes[rightLeft].height > nodes[rightRight].height) {
        nodes[right].right = rightLeft;
        nodes[node].right = rightRight;
        nodes[rightRight].parent = node;
        nodes[node].aabb.merge(nodes[left].aabb, nodes[rightRight].aabb);
        nodes[right].aabb.merge(nodes[node].aabb, nodes[rightLeft].aabb);

        nodes[node].height =
            1 + std::max(nodes[left].height, nodes[rightRight].height);
        nodes[right].height =
            1 + std::max(nodes[node].height, nodes[rightLeft].height);
      } else {
        nodes[right].right = rightRight;
        nodes[node].right = rightLeft;
        nodes[rightLeft].parent = node;
        nodes[node].aabb.merge(nodes[left].aabb, nodes[rightLeft].aabb);
        nodes[right].aabb.merge(nodes[node].aabb, nodes[rightRight].aabb);

        nodes[node].height =
            1 + std::max(nodes[left].height, nodes[rightLeft].height);
        nodes[right].height =
            1 + std::max(nodes[node].height, nodes[rightRight].height);
      }

      return right;
    }

    // Rotate left branch up.
    if (currentBalance < -1) {
      unsigned int leftLeft = nodes[left].left;
      unsigned int leftRight = nodes[left].right;

      assert(leftLeft < nodeCapacity);
      assert(leftRight < nodeCapacity);

      // Swap node and its left-hand child.
      nodes[left].left = node;
      nodes[left].parent = nodes[node].parent;
      nodes[node].parent = left;

      // The node's old parent should now point to its left-hand child.
      if (nodes[left].parent != NULL_NODE) {
        if (nodes[nodes[left].parent].left == node)
          nodes[nodes[left].parent].left = left;
        else {
          assert(nodes[nodes[left].parent].right == node);
          nodes[nodes[left].parent].right = left;
        }
      } else
        root = left;

      // Rotate.
      if (nodes[leftLeft].height > nodes[leftRight].height) {
        nodes[left].right = leftLeft;
        nodes[node].left = leftRight;
        nodes[leftRight].parent = node;
        nodes[node].aabb.merge(nodes[right].aabb, nodes[leftRight].aabb);
        nodes[left].aabb.merge(nodes[node].aabb, nodes[leftLeft].aabb);

        nodes[node].height =
            1 + std::max(nodes[right].height, nodes[leftRight].height);
        nodes[left].height =
            1 + std::max(nodes[node].height, nodes[leftLeft].height);
      } else {
        nodes[left].right = leftRight;
        nodes[node].left = leftLeft;
        nodes[leftLeft].parent = node;
        nodes[node].aabb.merge(nodes[right].aabb, nodes[leftLeft].aabb);
        nodes[left].aabb.merge(nodes[node].aabb, nodes[leftRight].aabb);

        nodes[node].height =
            1 + std::max(nodes[right].height, nodes[leftLeft].height);
        nodes[left].height =
            1 + std::max(nodes[node].height, nodes[leftRight].height);
      }

      return left;
    }

    return node;
  }

  //! Compute the height of the tree.
  /*! \return
          The height of the entire tree.
   */
  unsigned int computeHeight() const
  {
    return computeHeight(root);
  }

  //! Compute the height of a sub-tree.
  /*! \param node
          The index of the root node.

      \return
          The height of the sub-tree.
   */
  unsigned int computeHeight(unsigned int node) const
  {
    assert(node < nodeCapacity);

    if (nodes[node].isLeaf()) return 0;

    unsigned int height1 = computeHeight(nodes[node].left);
    unsigned int height2 = computeHeight(nodes[node].right);

    return 1 + std::max(height1, height2);
  }

  //! Assert that the sub-tree has a valid structure.
  /*! \param node
          The index of the root node.
   */
  void validateStructure(unsigned int node) const
  {
    if (node == NULL_NODE) return;

    if (node == root) assert(nodes[node].parent == NULL_NODE);

    unsigned int left = nodes[node].left;
    unsigned int right = nodes[node].right;

    if (nodes[node].isLeaf()) {
      assert(left == NULL_NODE);
      assert(right == NULL_NODE);
      assert(nodes[node].height == 0);
      return;
    }

    assert(left < nodeCapacity);
    assert(right < nodeCapacity);

    assert(nodes[left].parent == node);
    assert(nodes[right].parent == node);

    validateStructure(left);
    validateStructure(right);
  }

  //! Assert that the sub-tree has valid metrics.
  /*! \param node
          The index of the root node.
   */
  void validateMetrics(unsigned int node) const
  {
    if (node == NULL_NODE) return;

    unsigned int left = nodes[node].left;
    unsigned int right = nodes[node].right;

    if (nodes[node].isLeaf()) {
      assert(left == NULL_NODE);
      assert(right == NULL_NODE);
      assert(nodes[node].height == 0);
      return;
    }

    assert(left < nodeCapacity);
    assert(right < nodeCapacity);

    int height1 = nodes[left].height;
    int height2 = nodes[right].height;
    int height = 1 + std::max(height1, height2);
    (void)height;  // Unused variable in Release build
    assert(nodes[node].height == height);

    AABB aabb;
    aabb.merge(nodes[left].aabb, nodes[right].aabb);

    for (unsigned int i = 0; i < dimension; i++) {
      assert(aabb.lowerBound[i] == nodes[node].aabb.lowerBound[i]);
      assert(aabb.upperBound[i] == nodes[node].aabb.upperBound[i]);
    }

    validateMetrics(left);
    validateMetrics(right);
  }
};

}  // namespace abby2