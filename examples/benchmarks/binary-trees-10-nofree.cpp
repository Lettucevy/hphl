#include <cstdio>
#include <cstdlib>
class Node { public: int item; Node* left; Node* right; Node(int i): item(i), left(nullptr), right(nullptr) {} Node(int i, Node* l, Node* r): item(i), left(l), right(r) {} };
int itemCheck(Node* n) { if (n->left == nullptr) return n->item; return n->item + itemCheck(n->left) - itemCheck(n->right); }
Node* bottomUpTree(int i, int d) { if (d > 0) return new Node(i, bottomUpTree(2*i-1, d-1), bottomUpTree(2*i, d-1)); return new Node(i, nullptr, nullptr); }
int main() {
    int minD=4, maxD=10, sD=maxD+1;
    std::printf("Stretching tree of depth %d\n", sD);
    int ch = itemCheck(bottomUpTree(0, sD));
    std::printf("Item check: %d\n", ch);
    for (int depth=minD; depth<=maxD; depth+=2) {
        int it = 1 << (maxD-depth+minD);
        int sum = 0;
        for (int i=1; i<=it; i++) { sum += itemCheck(bottomUpTree(i, depth)) - itemCheck(bottomUpTree(-i, depth)); }
        std::printf("%d trees of depth %d item check: %d\n", it*2, depth, sum);
    }
    std::printf("Long lived tree of depth %d item check: %d\n", maxD, itemCheck(bottomUpTree(0, maxD)));
    return 0;
}
