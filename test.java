public class Main {
    public static void main(String[] args) {
        // 1. Mandatory CGI Headers (Notice the double \r\n to end the header section)
        System.out.print("Content-Type: text/plain\r\n\r\n");

        // 2. Your actual payload
        System.out.println("Hello, World from Java CGI!");
    }
}
