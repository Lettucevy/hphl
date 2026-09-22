// binary-trees-no-free.cpp — binary-trees em C++ SEM free (simula "GC only at end")
// Mostra o pico de memória se o GC só rodasse no epílogo (igual ao HP-HL atual).

#include <cstdio>
#include <cstdlib>

class Node {
public:
    int item;
    Node* left;
    Node* right;
    Node(int item) : item(item), left(nullptr), right(nullptr) {}
    Node(int item, Node* left, Node* right) : item(item), left(left), right(right) {}
};

int itemCheck(Node* node) {
    if (node->left == nullptr) return node->item;
    return node->item + itemCheck(node->left) - itemCheck(node->right);
}

Node* bottomUpTree(int item, int depth) {
    if (depth > 0) {
        return new Node(item,
                        bottomUpTree(2 * item - 1, depth - 1),
                        bottomUpTree(2 * item, depth - 1));
    }
    return new Node(item, nullptr, nullptr);
}

// Não faz free — simula "memória acumula até o final"
int main() {
    int minDepth = 4;
    int maxDepth = 14;
    int stretchDepth = maxDepth + 1;

    std::printf("Stretching tree of depth %d\n", stretchDepth);
    int check = itemCheck(bottomUpTree(0, stretchDepth));
    std::printf("Item check: %d\n", check);

    for (int depth = minDepth; depth <= maxDepth; depth += 2) {
        int iterations = 1 << (maxDepth - depth + minDepth);
        int sum = 0;
        for (int i = 1; i <= iterations; i++) {
            sum = sum + itemCheck(bottomUpTree(i, depth));
            sum = sum - itemCheck(bottomUpTree(-i, depth));
        }
        std::printf("%d trees of depth %d item check: %d\n", iterations * 2, depth, sum);
    }

    std::printf("Long lived tree of depth %d item check: %d\n",
                maxDepth, itemCheck(bottomUpTree(0, maxDepth)));
    return 0;
}
