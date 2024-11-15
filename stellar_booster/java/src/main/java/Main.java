import io.transwarp.stellardb_booster.*;

public class Main {

    static int sum(int[] arr) {
        int ans = 0;
        for (int i : arr) {
            ans += i;
        }
        return ans;
    }

    public static void main(String[] args) throws InterruptedException, BoosterObjectRefDestroyedException {

        int num = Integer.parseInt(args[0]);
        final int task_num = Integer.parseInt(args[1]);
        String sql = args[4];
//        BoosterDatabase db = new BoosterDatabase("/Users/mac/Documents/data/sf01_fuck", 0, true, true, 0, false, -1);
        BoosterDatabase db = new BoosterDatabase("/mnt/disk1/wq-data/sf100_33", 0, true, true, 0, false, 4096);


        int[] res = new int[num];
        final boolean[] continue_go = new boolean[1];
        continue_go[0] = true;

        Runtime.getRuntime().addShutdownHook(new Thread(() -> continue_go[0] = false));

        Thread[] tt = new Thread[num];
        for (int i = 0; i < num; i++) {
            int finalI = i;
            Thread thread = new Thread(() -> {
                BoosterConnection conn = new BoosterConnection(db);
                try {
                    conn.setMaxNumThreadForExec(task_num);
                    conn.query("call enable_gds=false; ").destroy();
                    while (continue_go[0]) {
                        res[finalI]++;
                        BoosterQueryResult ans = conn.query(sql);
                        if (!ans.isSuccess()) {
                            System.out.println(ans.getErrorMessage());
                        } else {
                            if (num == 1) {
                                BoosterFlatTuple x = ans.getNext();
                                System.out.println(x);
                                x.close();
                            } else {
                                while (ans.hasNext()) {
                                    ans.getNext().destroy();
                                }
                            }
                        }
                        ans.destroy();
                    }

                } catch (BoosterObjectRefDestroyedException e) {
                    throw new RuntimeException(e);
                }
                try {
                    conn.close();
                } catch (BoosterObjectRefDestroyedException e) {
                    throw new RuntimeException(e);
                }
            });
            tt[i] = thread;
            thread.start();
        }

        int pre = sum(res);
        for (int i = 0; i < 5; i++) {
            Thread.sleep(10000);
            int newSum = sum(res);
            double qps = newSum - pre;
            pre = newSum;
            System.out.println(qps / 10.0);
        }

        continue_go[0] = false;
        for (Thread t : tt) {
            t.join();
        }
        db.close();

    }
}
