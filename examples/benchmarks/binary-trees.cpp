// binary-trees.cpp — CLBG "binary-trees" benchmark em C++ (malloc/free manual)
// Equivalente direto ao binary-trees.hphl (mesma lógica, sem GC).

#include <cstdio>
#include <cstdlib>
#include <cstdint>

class Node {
public:
    int item;
    Node* left;
    Node* right;
    Node(int item) : item(item), left(nullptr), right(nullptr) {}
    Node(int item, Node* left, Node* right) : item(item), left(left), right(right) {}
    ~Node() {
        // destrutor recursivo: libera a subárvore inteira
        // (em C++ com std::unique_ptr isso seria automático; aqui é manual)
    }
};

int itemCheck(Node* node) {
    if (node->left == nullptr) {
        return node->item;
    }
    return node->item + itemCheck(node->left) - itemCheck(node->right);
}

// bottomUpTree aloca e retorna; caller é responsável por free
Node* bottomUpTree(int item, int depth) {
    if (depth > 0) {
        return new Node(item,
                        bottomUpTree(2 * item - 1, depth - 1),
                        bottomUpTree(2 * item, depth - 1));
    }
    return new Node(item, nullptr, nullptr);
}

// Libera a árvore inteira recursivamente
void freeTree(Node* node) {
    if (node == nullptr) return;
    freeTree(node->left);
    freeTree(node->right);
    delete node;
}

int main() {
    int minDepth = 4;
    int maxDepth = 14;
    int stretchDepth = maxDepth + 1;

    std::printf("Stretching tree of depth %d\n", stretchDepth);
    Node* stretchTree = bottomUpTree(0, stretchDepth);
    int check = itemCheck(stretchTree);
    freeTree(stretchTree);

    std::printf("Item check: %d\n", check);

    int longLivedTree = 0;
    for (int depth = minDepth; depth <= maxDepth; depth += 2) {
        int iterations = 1 << (maxDepth - depth + minDepth);
        int sum = 0;
        for (int i = 1; i <= iterations; i++) {
            Node* t1 = bottomUpTree(i, depth);
            Node* t2 = bottomUpTree(-i, depth);
            sum = sum + itemCheck(t1);
            sum = sum - itemCheck(t2);
            freeTree(t1);
            freeTree(t2);
        }
        std::printf("%d trees of depth %d item check: %d\n", iterations * 2, depth, sum);
    }

    Node* longLived = bottomUpTree(0, maxDepth);
    std::printf("Long lived tree of depth %d item check: %d\n", maxDepth, itemCheck(longLived));
    freeTree(longLived);
    return 0;
}
