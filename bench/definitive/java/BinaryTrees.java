public class BinaryTrees {
    static class Node {
        int item;
        Node left, right;
        Node(int i) { this.item = i; }
        Node(int i, Node l, Node r) { this.item = i; this.left = l; this.right = r; }
    }

    static int itemCheck(Node n) {
        if (n.left == null) return n.item;
        return n.item + itemCheck(n.left) - itemCheck(n.right);
    }

    static Node bottomUpTree(int i, int d) {
        if (d > 0) return new Node(i, bottomUpTree(2 * i - 1, d - 1), bottomUpTree(2 * i, d - 1));
        return new Node(i);
    }

    public static void main(String[] args) {
        int minD = 4, maxD = 14, sD = maxD + 1;
        System.out.printf("Stretching tree of depth %d%n", sD);
        Node st = bottomUpTree(0, sD);
        int ch = itemCheck(st);
        System.out.printf("Item check: %d%n", ch);

        for (int depth = minD; depth <= maxD; depth += 2) {
            int it = 1 << (maxD - depth + minD);
            int sum = 0;
            for (int i = 1; i <= it; i++) {
                Node t1 = bottomUpTree(i, depth);
                Node t2 = bottomUpTree(-i, depth);
                sum += itemCheck(t1) - itemCheck(t2);
            }
            System.out.printf("%d trees of depth %d item check: %d%n", it * 2, depth, sum);
        }

        Node ll = bottomUpTree(0, maxD);
        System.out.printf("Long lived tree of depth %d item check: %d%n", maxD, itemCheck(ll));
    }
}
